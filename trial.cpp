#include<thread>
#include<stack>
#include<iostream>
using namespace std;

int hi(){
	cout<<"hi ";
	return 0;
}
int main(){
	stack<thread> mystack;
	thread hot_thread = thread(hi);
	thread::id hot_thread_id = hot_thread.get_id();
	mystack.push(move(hot_thread));
	thread the_thread = move(mystack.top());
	mystack.pop();
	cout<<"this is the thread is "<<the_thread.get_id()<<" "<<hot_thread_id<<"\n";
	the_thread.join();

	return 0;
}
