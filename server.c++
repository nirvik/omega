#include<unistd.h>
#include<iostream>
#include<sys/socket.h>
#include<sys/types.h>
#include <stdlib.h>
#include <cstring>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <errno.h>
#include<sys/epoll.h>
#include<fcntl.h>
#include<string.h>
#define PORT 8000
#define MAXEVENTS 10

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

class TaskQueue {
	private:
		int rear;
		int front;
		Task* myq[10000];  // hopefully 10,000 connections
	public:
		TaskQueue(){
			rear = -1;
			front = -1;
		}

		void enqueue(Task *task){
			myq[++front] = task;
		}

		Task* dequeue() {
			Task *job = myq[++rear];
			return job;
		}

		bool isFull() {
			
		}

		bool isEmpty() {

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
		Task *newTask = createNewTask(shitty, fd);
		send(fd, reply, strlen(reply), 0);
	}
}

int main(int argc, const char *argv[]) {

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

	for(;;){
		int nfds = epoll_wait(epoll_fd, events, MAXEVENTS, -1);
		for(int i=0; i< nfds; i++){
			if(events[i].data.fd == socket_fd){
				if(createNewConnection(epoll_fd, ev, socket_fd)){
					std::cout<<"Created new connection!\n";
				}
			} else {
				read_the_fd(events[i].data.fd);
			}	
		}	
	}
	return 0;
}