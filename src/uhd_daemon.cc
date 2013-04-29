//#define DEBUG
#include <getopt.h>
#include "socketInterface.h"
#include "uhd_interface.h"
#include "sha1.h"
#include "base64.h"
#include <stdlib.h>
#include <pthread.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include <termios.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <string.h>
#include <iostream>

using namespace std;


//Return value errors
#define BAD_BIND 1

socketInterface tcp_interface(SOCKET_TCP);
socketInterface udp_interface(SOCKET_UDP);
socketInterface ws_interface(SOCKET_WS);
socketInterface tcp_cmd_interface(SOCKET_TCP);
socketInterface udp_cmd_interface(SOCKET_UDP);
socketInterface ws_cmd_interface(SOCKET_WS);
int serial_fd;
void sighandler(int sig){
	printf("lithiumd: signal %d caught...\n", sig);
	tcp_interface.closeFP();
	udp_interface.closeFP();
	ws_interface.closeFP();
	close(serial_fd);
	exit(1);
}

int main(int argc, char *argv[]){

	//In this thread, we'd like to ignore all SIGPIPE signals, and handle them separately
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGPIPE);
	pthread_sigmask(SIG_BLOCK, &set, NULL);

	struct sigaction new_action, old_action;
	/* Set up the structure to specify the new action. */
	new_action.sa_handler = sighandler;
	sigemptyset (&new_action.sa_mask);
	new_action.sa_flags = 0;
	
	//Set up signal handlers
	sigaction (SIGINT, NULL, &old_action);
	if (old_action.sa_handler != SIG_IGN)
		sigaction (SIGINT, &new_action, NULL);
		sigaction (SIGHUP, NULL, &old_action);
	if (old_action.sa_handler != SIG_IGN)
		sigaction (SIGHUP, &new_action, NULL);
		sigaction (SIGTERM, NULL, &old_action);
	if (old_action.sa_handler != SIG_IGN)
		sigaction (SIGTERM, &new_action, NULL);

	//Set up getopt
	const char* const short_options = "t:u:w:T:U:W:f:F:b:B:h";
	const struct option long_options[] = {
		{"tcp_data", required_argument, 0, 't'},
		{"udp_data", required_argument, 0, 'u'},
		{"ws_data", required_argument, 0, 'w'},
		{"tcp_cmd", required_argument, 0, 'T'},
		{"udp_cmd", required_argument, 0, 'U'},
		{"ws_cmd", required_argument, 0, 'W'},
		{"firmware", required_argument, 0, 'f'},
		{"fpga_image", required_argument, 0, 'F'},
		{"rx_bandwidth", required_argument, 0, 'b'},
		{"tx_bandwidth", required_argument, 0, 'B'},
		{"codec_highspeed", no_argument, 0, 'h'},
		{NULL, 0, NULL, 0}
	};//TODO: Need to add more arguments such as tuning frequency, sampling rate, etc.

	pthread_t tcp_conn_listener;
	pthread_t udp_conn_listener;
	pthread_t ws_conn_listener;
	pthread_t tcp_cmd_conn_listener;
	pthread_t udp_cmd_conn_listener;
	pthread_t ws_cmd_conn_listener;

	//Open up two separate sockets, one for commands, and one for data
	int tcp_portnum = 0;
	int udp_portnum = 0;
	int ws_portnum = 0;
	int tcp_cmd_portnum = 0;
	int udp_cmd_portnum = 0;
	int ws_cmd_portnum = 0;
	int rx_bandwidth = 1000000;
	int tx_bandwidth = 1000000;
	bool codec_highspeed = false;
	string uhd_arguments = "";

	//Parse out the arguments using getopt_long
	int option_index, next_option;
	while((next_option = getopt_long(argc, argv, short_options, long_options, NULL)) != -1){
		switch(next_option){
		case 't':
			tcp_portnum = atoi(optarg);
			break;
		case 'u':
			udp_portnum = atoi(optarg);
			break;
		case 'w':
			ws_portnum = atoi(optarg);
			break;
		case 'T':
			tcp_cmd_portnum = atoi(optarg);
			break;
		case 'U':
			udp_cmd_portnum = atoi(optarg);
			break;
		case 'W':
			ws_cmd_portnum = atoi(optarg);
			break;
		case 'f':
			uhd_arguments += ", fpga=";
			uhd_arguments += optarg;
			break;
		case 'F':
			uhd_arguments += ", fw=";
			uhd_arguments += optarg;
			break;
		case 'h':
			codec_highspeed = true;
			break;
		case 'b':
			rx_bandwidth = atoi(optarg);
			break;
		case 'B':
			tx_bandwidth = atoi(optarg);
			break;
		default:
			break;
		}
	}

	//Check to see if there are any uhd arguments supplied, and if so, remove the leading comma
	if(uhd_arguments[0] == ',')
		uhd_arguments = uhd_arguments.substr(2,uhd_arguments.length()-2);

	//Instantiate a UHD interface and link in with the created ports
//	uhdInterface usrp_instance(uhd_arguments,"","A:0","J1","J1",tx_bandwidth,rx_bandwidth,5630000000.0,5630000000.0,30.0,40.0, codec_highspeed);
	uhdInterface usrp_instance(uhd_arguments,"","A:0","J1","J1",tx_bandwidth,rx_bandwidth,2500000000.0,2500000000.0,20.0,40.0, codec_highspeed);

	//Run a thread which listens to the data socket (only for non-datagram interfaces (TCP, WS))
	if(tcp_portnum){
		tcp_portnum = newSocket(tcp_portnum, &tcp_interface, true);
		usrp_instance.registerDownstreamControlInterface(&tcp_interface, CONTROL_DATA);
		pthread_create(&tcp_conn_listener, NULL, socketConnectionListener, (void *)&tcp_interface);
	}
	//if(udp_portnum){
		//udp_portnum = newSocket(udp_portnum, &udp_interface, false);
		//usrp_instance.registerDownstreamControlInterface(&udp_interface, CONTROL_DATA);
		//pthread_create(&udp_conn_listener, NULL, socketConnectionListener, (void *)&udp_interface);
	//}
	if(ws_portnum){
		ws_portnum = newSocket(ws_portnum, &ws_interface, true);
		usrp_instance.registerDownstreamControlInterface(&ws_interface, CONTROL_DATA);
		pthread_create(&ws_conn_listener, NULL, socketConnectionListener, (void *)&ws_interface);
	}
	if(tcp_cmd_portnum){
		tcp_cmd_portnum = newSocket(tcp_cmd_portnum, &tcp_cmd_interface, true);
		usrp_instance.registerDownstreamControlInterface(&tcp_cmd_interface, CONTROL_CMD);
		pthread_create(&tcp_cmd_conn_listener, NULL, socketConnectionListener, (void *)&tcp_cmd_interface);
	}
	//if(udp_cmd_portnum){
		//udp_cmd_portnum = newSocket(udp_cmd_portnum, &udp_cmd_interface, false);
		//usrp_instance.registerDownstreamControlInterface(&udp_cmd_interface, CONTROL_CMD);
		//pthread_create(&udp_cmd_conn_listener, NULL, socketConnectionListener, (void *)&udp_cmd_interface);
	//}
	if(ws_cmd_portnum){
		ws_cmd_portnum = newSocket(ws_cmd_portnum, &ws_cmd_interface, true);
		usrp_instance.registerDownstreamControlInterface(&ws_cmd_interface, CONTROL_CMD);
		pthread_create(&ws_cmd_conn_listener, NULL, socketConnectionListener, (void *)&ws_cmd_interface);
	}

	
	//Now fork off because this is a daemon!
	//pid_t pid = fork();

	//Exit if this is not the child thread...
/*	if(pid != 0)
		return 0;

	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);*/

	//Now onto the child task stuff...
//	umask(0);
//	pid_t sid = setsid();

	while(1){
		sleep(30);
	}

	pthread_join(tcp_conn_listener, NULL);
	pthread_join(udp_conn_listener, NULL);
	pthread_join(ws_conn_listener, NULL);

	//Close all open socket connections
	tcp_interface.closeFP();
	udp_interface.closeFP();
	ws_interface.closeFP();

}
