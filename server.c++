#include<unistd.h>
#include<iostream>
#include<sys/socket.h>
#include<sys/types.h>
#include<stdlib.h>
#include<cstring>
#include<netinet/ip.h>
#include<arpa/inet.h>
#include<errno.h>
#include<sys/epoll.h>
#include<fcntl.h>
#include<string.h>
#include<map>
#include<stack>
#include<vector>
#include<thread>
#include<mutex>
#include<condition_variable>

#define PORT 8000
#define MAXEVENTS 10
#define QUEUE_LIMIT 10000
#define THREADS 4

struct Task {
	void (*func)(int);
	int param;
};

Task *createNewTask(void (*func)(int), int param){
	Task *x = new Task;
	x->func = func;
	x->param = param;
	return x;
}

// std::map<std::thread::id, std::pair<std::thread, int>> processing_threads;
std::map<int, std::thread> processing_threads;
std::stack<std::pair<std::thread, int>> threadPool;
std::thread threads[THREADS];
bool done[THREADS];

std::mutex mtx;
std::condition_variable cv;
int waiting_writers = 0;
int writers = 0;
int readers = 0;

class TaskQueue {
	private:
		int _rear;
		int _front;
		Task* myq[QUEUE_LIMIT];  // hopefully 10,000 connections
	public:
		TaskQueue(){
			_rear = 0;
			_front = 0;
		}

		void enqueue(Task *task){
			waiting_writers++;
			std::unique_lock<std::mutex> lk(mtx);
			cv.wait(lk, []() { return (writers == 0);  });

			// critical section
			writers++;
			myq[_rear++] = task;
			_rear = _rear % QUEUE_LIMIT;
			waiting_writers--;
			writers--;


			cv.notify_all();
		}

		void dequeue() {
			if(isEmpty()) return;
			waiting_writers++;
			std::unique_lock<std::mutex> lk(mtx);
			cv.wait(lk, [&]() { return (writers == 0 && !isEmpty()); });
			
			writers++;
			_front++;
			_front = _front % QUEUE_LIMIT;
			waiting_writers--;
			writers--;

			cv.notify_all();
			
		}

		Task* front() {
			/*
			if(isEmpty()){
				return NULL;
			}
			*/
			readers++;
			std::unique_lock<std::mutex> lk(mtx);
			cv.wait(lk, [&]() { return (waiting_writers == 0 && writers == 0 && !isEmpty());});
			Task *job = myq[_front];
			readers--;
			cv.notify_all();

			return job;
		}
	
		bool isEmpty() {
			return (_rear == _front);
		}

};



bool setNonBlocking(int fd){
	int flags = fcntl(fd, F_GETFL);
	flags |= O_NONBLOCK;
	int success = fcntl(fd, F_SETFL, flags);
	if(success == -1) return false;
	return true;
}

bool createNewConnection(int epoll_fd, struct epoll_event &ev, int socket_fd){
	struct sockaddr my_addr;
	socklen_t size_of_my_addr = sizeof(my_addr);
	/* This structure is filled in with the address of the peer socket*/
	int client_socket_fd = accept(socket_fd, &my_addr, &size_of_my_addr);
	if(client_socket_fd == -1) return false;
	setNonBlocking(client_socket_fd);

	ev.data.fd = client_socket_fd;
	ev.events = EPOLLIN;
	if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket_fd, &ev) == -1){
		std::cout<<"Failed to add client connection to epoll \n";
	}
	// add the fd to a thread pool 	
	return true;
}


void read_the_fd(int fd){
	char buff[512];
	int count = read(fd, buff, 512);
	char *reply =
		"HTTP/1.1 200 OK\n"
		"Date: Thu, 19 Feb 2009 12:27:04 GMT\n"
		"Server: Omega/1.0.0\n"
		"Content-Type: text/html\n"
		"Content-Length: 22\n"
		"Accept-Ranges: bytes\n"
		"Connection: close\n"
		"\n"
		"<h1>Hello World !</h1>";
	if(count != 0 && count != -1){
		// std::cout<<buff<<" ";
		send(fd, reply, strlen(reply), 0);
	}
	close(fd);
}


std::mutex done_mtx;
int done_readers = 0;
int done_writers = 0;
int done_waiting_writers = 0;

int worker(TaskQueue *tq, int thread_id){
	Task* job = tq->front();
        tq->dequeue();
        if(job == NULL) {
		std::cout<<"No work yet \n";
		return 0;
	}
        int fd = job->param;
        job->func(fd);

	done_waiting_writers++;
	std::unique_lock<std::mutex> lk(done_mtx);
	cv.wait(lk, []() { return (done_writers == 0); });
	done_waiting_writers--;
	done_writers++;
	done[thread_id] = true;
	done_writers--;

	cv.notify_all();

        return 0;


}


void backgroundWorkers(TaskQueue *tq){

	while(1){
		std::vector<int> keys;
		while(!threadPool.empty()){
			
			std::pair<std::thread, int> hot_thread = make_pair(move(threadPool.top().first), threadPool.top().second);
			threadPool.pop();
			int index = hot_thread.second;
			hot_thread.first = std::thread(worker, tq, index);
			//std::thread::id hot_thread_id = hot_thread.first.get_id();
			//processing_threads[hot_thread_id] = make_pair(move(hot_thread.first), hot_thread.second); 
			processing_threads[hot_thread.second] = move(hot_thread.first);

		}
		for(auto &dirs: processing_threads){
			int index = dirs.first; // dirs.second.second
			done_readers++;
			std::unique_lock<std::mutex> lk(done_mtx);
			cv.wait(lk, [](){return (done_writers == 0 && done_waiting_writers == 0);});
			if(done[index]){
				dirs.second.join();
				threadPool.push(make_pair(move(threads[index]), index));
				done[index] = false;
				std::cout<<"Pushing another thread "<<index<<"\n";
				keys.push_back(dirs.first);
			}
			done_readers--;
			cv.notify_all();
		}
		for(auto key: keys){
			processing_threads.erase(key);
		}
	}
}

int main(int argc, const char *argv[]) {


	TaskQueue *tq = new TaskQueue();
	for(int i=0; i < THREADS; i++){
		threadPool.push(make_pair(move(threads[i]), i));
		done[i] = false;
	}

	// define the socket
	int socket_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);

	// set socket options
	int opt_val = 1;
	setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(opt_val));

	//binding
	struct sockaddr_in my_addr; // socket address 
	const char *ip_addr = "127.0.0.1";
	memset(&my_addr, 0,sizeof(struct sockaddr_in));
	my_addr.sin_family  = AF_INET;
	my_addr.sin_port = htons(PORT);
	inet_aton(ip_addr, &my_addr.sin_addr); // in_addr 32bits 

	if(bind(socket_fd, (sockaddr *)&my_addr, sizeof(my_addr)) == -1){
		std::cout<<" Failed to bind: "<<strerror(errno);
	} else {
		std::cout<<" Binding done \n";
	}
	if(listen(socket_fd, 256) < 0){
		std::cout<<"failed to listen "<<strerror(errno);
	}
	std::cout<<"Waiting for connections !\n";

	int epoll_fd = epoll_create1(0);
	if(epoll_fd == -1){
		std::cout<<"Failed to create epoll instance ";
	}
	struct epoll_event ev, events[MAXEVENTS];
	ev.events = EPOLLIN;
	ev.data.fd = socket_fd;

	if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, socket_fd, &ev) == -1){
		std::cout<<"Failed to epoll ctl ! ";
	}


	std::thread backgroundThread(backgroundWorkers, tq);
	for(;;){
		int nfds = epoll_wait(epoll_fd, events, MAXEVENTS, -1);
		for(int i=0; i< nfds; i++){
			if(events[i].data.fd == socket_fd){
				if(createNewConnection(epoll_fd, ev, socket_fd)){
					// std::cout<<"Created new connection!\n";
				}
			} else {
				 // read_the_fd(epoll_fd, events[i].data.fd);
				// add to task queue and deregister fd from epoll
				epoll_ctl(epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, NULL);
				tq->enqueue(createNewTask(read_the_fd, events[i].data.fd));
			}	
		}
		/*
		// start processing all requests in the queue
		for(int i=0; i< THREADS; i++){
			if(threads[i].joinable()) threads[i].join();  // still not a good idea
			threads[i] = std::thread(worker, tq, i+1);
		}
		*/
	}
	return 0;
}
