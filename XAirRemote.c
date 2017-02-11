/*
 * XAirRemote.c
 *
 *  Created on: 20 mars 2015 (X32Reaper.c)
 *      Author: Patrick-Gilles Maillot
 *
 * Copyright (c) 2015 Patrick-Gilles Maillot
 *
 *  Ported to XAir Series on: 11 Nov 2016 (XAirRemote.c)
 *      Author: Ken Mitchell
 *
 *  Port Modifications Copyright (c) 2016 Ken Mitchell
 *  
 *  - Renamed to XAirRemote vs. XAirReaper since I plan on
 *    making it a more general purpose tool. 
 * 
 *   This code is distributed as Freeware for non-commercial users.
 *   Commercial users should contact myself and Patrick before using
 *   this utility or distributing this code.
 *
 */
int version_major = 1;
int version_minor = 2;
char version_letter = ' ';
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include "XAirRemote.h"

#ifdef __WIN32__
#include <winsock2.h>
#define MySleep(a)	Sleep(a)
#define socklen_t	int
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define closesocket(s) 	close(s)
#define MySleep(a)	usleep(a*1000)
#define WSACleanup()
#define SOCKET_ERROR -1
typedef int SOCKET;
typedef struct sockaddr_in SOCKADDR_IN;
typedef struct sockaddr SOCKADDR;
typedef struct in_addr IN_ADDR;
#endif

#define BSIZE 			512	// Buffer sizes (enough to take into account FX parameters)
//
// XAir to communicate on XAir_IP (192.168.0.64) and XAir_port (10024)
// Remote to communicate on Rem_IP (192.168.0.64) and Remport (10025)
//
// Buffers: Xb_r, Xb_s, Xb_lr, Xb_ls - read, send and lengths for XAir
// Buffers: Rb_r, Rb_s, Rb_lr, Rb_ls - read, send and lengths for Remote
// Defines: XBsmax, RBsmax, XBrmax, RBrmax - maximum lengths for buffers
//
#define XBsmax	BSIZE
#define RBsmax	BSIZE
#define XBrmax	BSIZE * 2
#define RBrmax	BSIZE * 4
//
// Private functions
int		XAirConnect();
void	XRcvClean();
void	Xlogf(char *header, char *buf, int len);
int		XAirParseRemoteMessage();
int		XAirParseXAirMessage();
void	XAirGetCurrentValue();
//
// External calls used
extern int Xsprint(char *bd, int index, char format, void *bs);
extern int Xfprint(char *bd, int index, char* text, char format, void *bs);
//
//
// Communication macros
//
#define SEND_TOX(b, l)											\
	do {														\
		if (Xverbose) Xlogf("->X", b, l);						\
		if (sendto(Xfd, b, l, 0, XXAirIP_pt, XXAirIP_len) < 0) {\
			fprintf(log_file, "****Error sending data to XAir\n");	\
		} 														\
		if (Xdelay > 0) MySleep(Xdelay);						\
	} while (0);
//
//
#define SEND_TOR(b, l)											\
	do {														\
		if (Xverbose) Xlogf("->R", b, l);						\
		if (sendto(Rfd, b, l, 0, RHstIP_pt, RHstIP_len) < 0) {	\
			fprintf(log_file, "****Error sending data to REMOTE\n");\
		} 														\
	} while (0);
//
//
// type cast union
union littlebig {
	char cc[4];
	int ii;
	float ff;
} endian;
//
int Xb_ls;
char Xb_s[XBsmax];
int Xb_lr;
char Xb_r[XBrmax];
int Rb_lr;
char Rb_r[RBrmax];
int Rb_ls;
char Rb_s[RBsmax];
//
int loop_toggle = 0x00; // toggles between 0x00 and 0x7f
//
char	S_SndPort[8], S_RecPort[8], S_XAir_IP[20], S_Hst_IP[20], S_Frm_IP[20];
//char	Xlogpath[LENPATH];
//
int zero = 0;
int one = 1;
int two = 2;
int six4 = 64;
int option = 1;
int play = 0;
int play_1 = 0;
// Misc. flags
int MainLoopOn = 1;		// main loop flag
int Xconnected = 0;		// 1 when communication is running
int Xverbose, Xdelay;	// verbose, List of Action and delay
int Xtransport_on = 1;	// whether transport is enabled or not (bank C)
int Xmaster_on = 1;		// whether master is enabled or not
int bus_offset = 0;		// offset to manage REMOTE track sends logical numbering
int Xsync_on = 1;		// whether to Sync XAir Values with Remote
//
int Xtrk_min = 0;		// Input min track number for Remote/XAir
int Xtrk_max = 0;		// Input max track number for Remote/XAir
int Xaux_min = 0;		// Aux min track number for Remote/XAir
int Xaux_max = 0;		// Aux max track number for Remote/XAir
int Xfxr_min = 0;		// FXrtn min track number for Remote/XAir
int Xfxr_max = 0;		// FXrtn max track number for Remote/XAir
int Xbus_min = 0;		// Bus min track number for Remote/XAir
int Xbus_max = 0;		// Bus max track number for Remote/XAir
int Xfxs_min = 0;		// FXsnd min track number for Remote/XAir
int Xfxs_max = 0;		// FXsnd max track number for Remote/XAir
int Xdca_min = 0;		// DCA min track number for Remote/XAir
int Xdca_max = 0;		// DCA max track number for Remote/XAir

//
struct ifaddrs *ifa;									// to get our own system's IP address
struct sockaddr_in XXAirIP;								// X socket IP we send/receive
struct sockaddr *XXAirIP_pt = (struct sockaddr*) &XXAirIP;// X socket IP pointer we send/receive
struct sockaddr_in RHstIP;								// R socket IP we send to
struct sockaddr *RHstIP_pt = (struct sockaddr*) &RHstIP;// R socket IP pointer we send to
struct sockaddr_in RFrmIP;								// R socket IP we received from
struct sockaddr *RFrmIP_pt = (struct sockaddr*) &RFrmIP;// R socket IP pointer we received from
int Xfd, Rfd, Mfd;			// XAir and Remote receive and send sockets; Mfd is max(Xfd, Rfd)+1 for select()

time_t before, now;			// timers for Xremote controls
int poll_status;			// Polling status flag
//
//
int XXAirIP_len = sizeof(XXAirIP);	// length of XAir address
int RHstIP_len = sizeof(RHstIP);	// length of Remote send to address
int RFrmIP_len = sizeof(RFrmIP);	// length of Remote received from address

#ifdef __WIN32__
WSADATA wsa;
#endif

fd_set 			fds;

struct timeval	timeout;
//unsigned long mode;
//
// resource and log file data
FILE *res_file;
FILE *log_file;
char ini_filename[256];	// Name of .ini file to load (Default: XAirRemote.ini)
char log_filename[256];	// Name of .log file to create (Default: XAirRemote.log)

//-------------------------------------------------------------------------
//

int main(int argc, char **argv) {

	int sync_count = 0;
	Xverbose = Xdelay = 0;
	
	strcpy(S_XAir_IP, "");
	strcpy(S_Hst_IP, "");
	
	// See if an INI/Log filename was passed in. 
	//
	// ./XAirRemote <instance> will cause <instance>.ini to be loaded
	// and <instance>.log to be created.
	//
	if (argc > 2) {
		printf("This program takes up to one (1) command line parameter!\n\n");
		printf("Usage: XAirRemote [instance]\n\n");
		printf("        [instance] - optional .ini/.log filename (default: XAirRemote)\n\n");
		exit(EXIT_FAILURE);
	} else if (argc > 1) {
	//  Set INI and LOG filenames here.  XXX: Should probably check for illegal chars.
		sprintf(ini_filename, "%s.ini", argv[1]);
		sprintf(log_filename, "%s.log", argv[1]);
	} else {
	//  Set Default INI and LOG filenames here
		sprintf(ini_filename, "XAirRemote.ini");
		// printf("%s\n", ini_filename);
		sprintf(log_filename, "XAirRemote.log");
		// printf("%s\n", log_filename);
	}
	// load resource file
	if ((res_file = fopen(ini_filename, "r")) != NULL) {
		fscanf(res_file, "%d %d %d %d %d\n", &Xverbose, &Xdelay, &Xtransport_on, &Xmaster_on, &Xsync_on);
		fscanf(res_file, "%d %d %d %d %d %d %d %d %d %d %d %d %d\n", &Xtrk_min, &Xtrk_max, &Xaux_min, &Xaux_max, &Xfxr_min, &Xfxr_max, &Xbus_min, &Xbus_max, &Xfxs_min, &Xfxs_max, &Xdca_min, &Xdca_max, &bus_offset);
		fgets(S_XAir_IP, sizeof(S_XAir_IP), res_file);
		S_XAir_IP[strlen(S_XAir_IP) - 1] = 0;
		fgets(S_Hst_IP, sizeof(S_Hst_IP), res_file);
		S_Hst_IP[strlen(S_Hst_IP) - 1] = 0;
		fgets(S_Frm_IP, sizeof(S_Frm_IP), res_file);
		S_Frm_IP[strlen(S_Frm_IP) - 1] = 0;
		fgets(S_SndPort, sizeof(S_SndPort), res_file);
		S_SndPort[strlen(S_SndPort) - 1] = 0;
		fgets(S_RecPort, sizeof(S_RecPort), res_file);
		S_RecPort[strlen(S_RecPort) - 1] = 0;
		fclose(res_file);
	} else {
		printf("Can't open INI file: %s\n", ini_filename);
		exit(EXIT_FAILURE);
	}
	printf("XAirRemote - v%d.%d%c - (c)2017 Ken Mitchell\n\n", version_major, version_minor, version_letter);
	printf("Successfully Loaded: %s\n", ini_filename);
	printf("XAir Mixer at IP %s\n", S_XAir_IP);
	printf("OSC Controller at IP %s, receives on port %s, sends to port %s\n", S_Hst_IP, S_RecPort, S_SndPort);
	printf("Listening for OSC Controller on %s:%s\n", S_Frm_IP, S_SndPort);
	printf("Flags: verbose: %1d, delay: %dms, Transport: %1d, Master: %1d, Sync: %1d\n", Xverbose, Xdelay, Xtransport_on, Xmaster_on, Xsync_on);
//  For now assume tracks are mapped in /-stat/solosw order
//	printf("Map (min/max): Ch %d/%d, Aux %d/%d, FxR %d/%d, Bus %d/%d, FxS %d/%d, DCA %d/%d, Bus Offset %d\n",
//			Xtrk_min, Xtrk_max, Xaux_min, Xaux_max, Xfxr_min, Xfxr_max, Xbus_min, Xbus_max, Xfxs_min, Xfxs_max, Xdca_min, Xdca_max, bus_offset);
	fflush(stdout);
	if ((log_file = fopen(log_filename, "w")) != NULL) {
		fprintf(log_file, "*******************************************************************************************\n");
	    fprintf(log_file, "  XAirRemote - v%d.%d%c - (c)2017 Ken Mitchell\n\n", version_major, version_minor, version_letter);
	    fprintf(log_file, "  Successfully Loaded: %s\n", ini_filename);
	    fprintf(log_file, "  XAir Mixer at IP %s\n", S_XAir_IP);
	    fprintf(log_file, "  OSC Controller at IP %s, receives on port %s, sends to port %s\n", S_Hst_IP, S_RecPort, S_SndPort);
   	    fprintf(log_file, "  Listening for OSC Controller on %s:%s\n", S_Frm_IP, S_SndPort);
	    fprintf(log_file, "  Flags: verbose: %1d, delay: %dms, Transport: %1d, Master: %1d, Sync: %1d\n", Xverbose, Xdelay, Xtransport_on, Xmaster_on, Xsync_on);
	    fprintf(log_file, "  Map (min/max): Ch %d/%d, Aux %d/%d, FxR %d/%d, Bus %d/%d, FxS %d/%d, DCA %d/%d, Bus Offset %d\n\n",
			Xtrk_min, Xtrk_max, Xaux_min, Xaux_max, Xfxr_min, Xfxr_max, Xbus_min, Xbus_max, Xfxs_min, Xfxs_max, Xdca_min, Xdca_max, bus_offset);
		fprintf(log_file, "*******************************************************************************************\n");
		fprintf(log_file, "  Start of Log data:\n\n");
		MainLoopOn = 1;
// If connected/running, Consume XAir and REMOTE messages
		if ((Xconnected = XAirConnect()) != 0) {		//
		
			// run SW as long as needed
			while (MainLoopOn) {
				now = time(NULL); 			// get time in seconds
				if (now > before + 9) { 	// need to keep /xremotenfb alive?
					Xb_ls = Xsprint(Xb_s, 0, 's', "/xremotenfb");
					SEND_TOX(Xb_s, Xb_ls)
					before = now;
				}

			// Sync Requested
				if (Xsync_on) { 
					sync_count = Xsync_size - 1;
					Xsync_on = 0;
				}	
			// Are we processing a Sync Request?	
				if(sync_count) {
					XAirGetCurrentValue(sync_count);
					sync_count -= 1;
				} 
//
// Update on the XAir or Remote?
				FD_ZERO(&fds);
				FD_SET(Rfd, &fds);
				FD_SET(Xfd, &fds);
				Mfd = Rfd + 1;
				if (Xfd > Rfd) Mfd = Xfd + 1;
				if (select(Mfd, &fds, NULL, NULL, &timeout) > 0) {
					if (FD_ISSET(Rfd, &fds) > 0) {
						if ((Rb_lr = recvfrom(Rfd, Rb_r, RBrmax, 0, RFrmIP_pt, (socklen_t *)&RFrmIP_len)) > 0) {
// Parse Remote Messages and send corresponding data to XAir
// These can be simple or #bundle messages!
// Can result in several/many messages to XAir
//							printf("REMOTE sent data\n"); fflush(stdout);
							if (Xverbose) Xlogf("R->", Rb_r, Rb_lr);
							Xb_ls = XAirParseRemoteMessage();
						}
					}
					if (FD_ISSET(Xfd, &fds) > 0) {
						if ((Xb_lr = recvfrom(Xfd, Xb_r, XBrmax, 0, XXAirIP_pt, (socklen_t *)&XXAirIP_len)) > 0) {
// XAir transmitted something
// Parse and send (if applicable) to Remote
//							printf("XAir sent data\n"); fflush(stdout);
							if (Xverbose) Xlogf("X->", Xb_r, Xb_lr);
							Rb_ls = XAirParseXAirMessage();
						}
					}
				}
			}
		}
	} else {
		printf("Cannot create LOG file: %s\n", log_filename);
		MySleep(10000);
	}
	WSACleanup();
	return 0;
}
//---------------------------------------------------------------------------------
//
//
// Private functions:
//
//
int XAirConnect() {
	int i;
//
	poll_status = 0;
//
	if (Xconnected) {
//
// Signing OFF
		Xb_ls = Xsprint(Xb_s, 0, 's', "/unsubscribe");
		if (Xverbose)
			Xlogf("->X", Xb_s, Xb_ls);
		if (sendto(Xfd, Xb_s, Xb_ls, 0, XXAirIP_pt, XXAirIP_len) < 0)
			fprintf(log_file, "XAirConnect: Error during shutdown while sending /unsubscribe message to XAir\n");
		WSACleanup();
		return 0;
	} else {
//
// Initialize winsock / communication with XAir server at IP ip and PORT port
#ifdef __WIN32__
		if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
			fprintf(log_file, "XAirConnect: WSA Startup Error\n");
			exit(EXIT_FAILURE);
		}
#endif
//
// Load the XAir address we connect to; we're a client to XAir, keep it simple.
		// Create the UDP socket
		if ((Xfd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
			fprintf(log_file, "XAirConnnect: Socket creation error\n");
			WSACleanup();
			return 0; // Make sure we aren't considered being connected
		} else {
			// Construct the server sockaddr_in structure
			memset(&XXAirIP, 0, sizeof(XXAirIP));			// Clear struct
			XXAirIP.sin_family = AF_INET;					// Internet/IP
			XXAirIP.sin_addr.s_addr = inet_addr(S_XAir_IP);	// IP address
			XXAirIP.sin_port = htons(atoi("10024"));		// XAir port
//
// Non blocking mode; Check for XAir connectivity
			Xb_ls = Xsprint(Xb_s, 0, 's', "/info");
			if (Xverbose)
				Xlogf("->X", Xb_s, Xb_ls);
			if (sendto(Xfd, Xb_s, Xb_ls, 0, XXAirIP_pt, XXAirIP_len) < 0) {
				fprintf(log_file, "XAirConnect: Error sending data to XAir\n");
				WSACleanup();
				return 0; // Make sure we aren't considered being connected
			} else {
				timeout.tv_sec = 0;
				timeout.tv_usec = 100000; //Set timeout for non blocking recvfrom(): 100ms
				FD_ZERO(&fds);
				FD_SET(Xfd, &fds);
//				printf("before select\n"); fflush(stdout);
				if ((poll_status = select(Xfd + 1, &fds, NULL, NULL, &timeout)) != -1) {
					if (FD_ISSET(Xfd, &fds) > 0) {
//						printf("after select\n"); fflush(stdout);
						// We have received data - process it!
						Xb_lr = recvfrom(Xfd, Xb_r, BSIZE, 0, 0, 0);
						if (Xverbose)
							Xlogf("X->", Xb_r, Xb_lr);
						if (strcmp(Xb_r, "/info") != 0) {
							fprintf(log_file, "XAirConnect: Unexpected answer from XAir\n");
							WSACleanup();
							return 0;
						}
					} else {
						// time out... Not connected or Not an XAir
						fprintf(log_file, "XAirConnect: XAir reception timeout\n");
						WSACleanup();
						return 0; // Make sure we aren't considered being connected
					}
				} else {
					fprintf(log_file, "XAirConnect: Polling for data failed\n");
					WSACleanup();
					return 0; // Make sure we aren't considered being connected
				}
			}
		}
	}
// printf("XAir Connected 1\n"); fflush(stdout);
//
// XAir connectivity OK. Set Connection with REMOTE as Hst (HOST)
// Connect / Bind HOST
	i = 0;
	if ((Rfd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) >= 0) {
		i = 1;
		if (setsockopt(Rfd, SOL_SOCKET, SO_REUSEADDR, (char*) &option,
				sizeof(option)) >= 0) {

			// Construct the server sockaddr_in structure
			memset(&RHstIP, 0, sizeof(RHstIP)); /* Clear struct */
			memset(&RFrmIP, 0, sizeof(RFrmIP)); /* Clear struct */
			RHstIP.sin_family = RFrmIP.sin_family = AF_INET; /* Internet/IP */
			RHstIP.sin_addr.s_addr = inet_addr(S_Hst_IP); /* Remote IP address */
			RFrmIP.sin_addr.s_addr = inet_addr(S_Frm_IP); /* Our Local IP address, may be the same if remote is running locally */
			RFrmIP.sin_port = htons(atoi(S_SndPort)); /* The Remote port we receive from */
			RHstIP.sin_port = htons(atoi(S_RecPort)); /* The Remote port we send to */
			i = 2;
			if (bind(Rfd, RFrmIP_pt, sizeof(RFrmIP)) != SOCKET_ERROR) {
				timeout.tv_sec = 0;
				timeout.tv_usec = 1000; //Set/Change timeout for non blocking recvfrom(): 1ms for Remote/XAir comm
//
// Cleanup XAir buffers if needed
				XRcvClean();
				printf("XAir Mixer found and connected!\n"); fflush(stdout);
				return 1; // We are connected!
			}
		}
	}
	// If we're here, there was an error
	printf("Remote socket error %d\n", i);
	fprintf(log_file, "Remote socket error %d\n", i);
	WSACleanup();
	return 0; // Make sure we aren't considered being connected
}
//
// Empty any pending message from XAir function
//
void XRcvClean() {
//
	if (Xverbose)
		fprintf(log_file, "XAir receive buffer cleanup if needed\n");
	do {

		FD_ZERO(&fds);
		FD_SET(Xfd, &fds);
		poll_status = select(0, &fds, NULL, NULL, &timeout);

		if (poll_status > 0) {
			if ((Xb_lr = recvfrom(Xfd, Xb_r, BSIZE, 0, 0, 0)) > 0) {
				if (Xverbose)
					Xlogf("X->", Xb_r, Xb_lr);
			}
		}
	} while (poll_status > 0);
	return;
}
//
//

//
//
void Xlogf(char *header, char *buf, int len) {
	int i, k, n, j, l, comma = 0, data = 0, dtc = 0;
	unsigned char c;

	fprintf(log_file, "%s, %4d B: ", header, len);
	for (i = 0; i < len; i++) {
		c = (unsigned char) buf[i];
		if (c < 32 || c == 127 || c == 255)
			c = '~'; // Manage unprintable chars
		fprintf(log_file, "%c", c);
		if (c == ',') {
			comma = i;
			dtc = 1;
		}
		if (dtc && (buf[i] == 0)) {
			data = (i + 4) & ~3;
			for (dtc = i + 1; dtc < data; dtc++) {
				if (dtc < len) {
					fprintf(log_file, "~");
				}
			}
			dtc = 0;
			l = data;
			while (++comma < l && data < len) {
				switch (buf[comma]) {
				case 's':
					k = (strlen((char*) (buf + data)) + 4) & ~3;
					for (j = 0; j < k; j++) {
						if (data < len) {
							c = (unsigned char) buf[data++];
							if (c < 32 || c == 127 || c == 255)
								c = '~'; // Manage unprintable chars
							fprintf(log_file, "%c", c);
						}
					}
					break;
				case 'i':
					for (k = 4; k > 0; endian.cc[--k] = buf[data++])
						;
					fprintf(log_file, "[%6d]", endian.ii);
					break;
				case 'f':
					for (k = 4; k > 0; endian.cc[--k] = buf[data++])
						;
					if (endian.ff < 10.)
						fprintf(log_file, "[%06.4f]", endian.ff);
					else if (endian.ff < 100.)
						fprintf(log_file, "[%06.3f]", endian.ff);
					else if (endian.ff < 1000.)
						fprintf(log_file, "[%06.2f]", endian.ff);
					else if (endian.ff < 10000.)
						fprintf(log_file, "[%06.1f]", endian.ff);
					break;
				case 'b':
					// Get the number of bytes
					for (k = 4; k > 0; endian.cc[--k] = buf[data++]);
					n = endian.ii;
					// Get the number of data (floats or ints ???) in little-endian format
					for (k = 0; k < 4; endian.cc[k++] = buf[data++]);
					if (n == endian.ii) {
						// Display blob as string
						fprintf(log_file, "%d chrs: ", n);
						for (j = 0; j < n; j++) fprintf(log_file, "%c ", buf[data++]);
					} else {
						// Display blob as floats
						n = endian.ii;
						fprintf(log_file, "%d flts: ", n);
						for (j = 0; j < n; j++) {
							//floats are little-endian format
							for (k = 0; k < 4; endian.cc[k++] = buf[data++]);
							fprintf(log_file, "%06.2f ", endian.ff);
						}
					}
					break;
				default:
					break;
				}
			}
			i = data - 1;
		}
	}
	fprintf(log_file, "\n");
	fflush (log_file);
}
//---------------------------
// Used to Sync values
//
void XAirGetCurrentValue(int Xsync_index) {
	Xb_ls = Xsprint(Xb_s, 0, 's', Xsync_data[Xsync_index]);
	SEND_TOX(Xb_s, Xb_ls)
	MySleep(20);
	return;
}

//--------------------------------------------------------------------
// XAir Messages data parsing
//
//
int XAirParseXAirMessage() {

	int Xb_i = 0;
	int Rb_ls = 0;
	int cnum, bus, dca, fxs, i;
	char tmp[32];

	float fone = 1.0;

// What is the XAir message made of?
// XAir format is:
//	/ch/%02d/mix/pan..........,f..[float]		%02d = 01..16
//	/ch/%02d/mix/fader........,f..[float]		%02d = 01..16
//	/ch/%02d/mix/on...........,i..[0/1]			%02d = 01..16
//	/ch/%02d/config/name......,s..[string\0]	%02d = 01..16
//	/ch/%02d/mix/%02d/level...,f..[float]	%02d = 01..16 / %02d = 01..10
//	/ch/%02d/mix/%02d/pan.....,f..[float]	%02d = 01..16 / %02d = 01, 03, 05
//
// Same applies to /rtn/aux and /rtn as for /ch above
//
//	/-stat/selidx.........,i..[%d]
//	/-stat/solosw/%02d....,i..[0/1]
//
//	/lr/mix/fader....,f..[float]
//	/lr/mix/pan......,f..[float]
//  /lr/mix/on.......,i..[0/1]
//
//	/bus/1..6/mix/pan......,f..[float]
//	/bus/1..6/mix/fader....,f..[float]
//	/bus/1..6/mix/on.......,i..[0/1]
//	/bus/1..6/config/name..,s..[string\0]
//
//	/dca/1..4/on...........,i..[0/1]
//	/dca/1..4/fader........,f..[0..1.0]
//	/dca/1..4/config/name..,s..[string\0]
//
// Same applies to /fxsend as for /dca above
//
//
	Rb_ls = 0;
	if (strncmp(Xb_r, "/ch/", 4) == 0) {
		// /ch/ cases : get channel number
		Xb_i = 4;
		cnum = (int) Xb_r[Xb_i++] - (int) '0';
		cnum = cnum * 10 + (int) Xb_r[Xb_i++] - (int) '0';
		if ((Xtrk_max > 0) && (cnum <= (Xtrk_max - Xtrk_min + 1))) {
			Xb_i += 1; // skip '/'
			if (Xb_r[Xb_i] == 'm') {
				Xb_i += 4; //skip "/mix/" string
				if (Xb_r[Xb_i] == 'p') {
					//	/ch/%02d/mix/pan......,f..[float]
					while (Xb_r[Xb_i] != ',') Xb_i += 1;
					Xb_i += 4;
					for (i = 4; i > 0; endian.cc[--i] = Xb_r[Xb_i++]);
					sprintf(tmp, "/track/%d/pan", cnum + Xtrk_min - 1);
					Rb_ls = Xfprint(Rb_s, 0, tmp, 'f', &endian.ff);
				} else if (Xb_r[Xb_i] == 'f') {
					//	/ch/%02d/mix/fader....,f..[float]
					while (Xb_r[Xb_i] != ',') Xb_i += 1;
					Xb_i += 4;
					for (i = 4; i > 0; endian.cc[--i] = Xb_r[Xb_i++]);
					sprintf(tmp, "/track/%d/volume", cnum + Xtrk_min - 1);
					Rb_ls = Xfprint(Rb_s, 0, tmp, 'f', &endian.ff);
				} else if (Xb_r[Xb_i] == 'o') {
					//	/ch/%02d/mix/on.......,i..[0/1]
					while (Xb_r[Xb_i] != ',') Xb_i += 1;
					Xb_i += 4;
					for (i = 4; i > 0; endian.cc[--i] = Xb_r[Xb_i++]);
					if (endian.ii == 1) endian.ff = 0.0;
					else				endian.ff = 1.0;
					sprintf(tmp, "/track/%d/mute", cnum + Xtrk_min - 1);
					Rb_ls = Xfprint(Rb_s, 0, tmp, 'f', &endian.ff);
				} else if ((Xb_r[Xb_i] == '0') || (Xb_r[Xb_i] == '1')) {
					// "/mix/" is followed by a Bus send number
					// get bus number
					bus = (int) Xb_r[Xb_i++] - (int) '0';
					bus = bus * 10 + (int) Xb_r[Xb_i++] - (int) '0';
					bus += bus_offset;
					Xb_i += 1; // skip '/'
					if (Xb_r[Xb_i] == 'l') {
						//	/ch/%02d/mix/%02d/level....,f..[float]
						while (Xb_r[Xb_i] != ',') Xb_i += 1;
						Xb_i += 4;
						for (i = 4; i > 0; endian.cc[--i] = Xb_r[Xb_i++]);
						sprintf(tmp, "/track/%d/send/%d/volume", cnum + Xtrk_min - 1, bus);
						Rb_ls = Xfprint(Rb_s, 0, tmp, 'f', &endian.ff);
					} else if (Xb_r[Xb_i] == 'p') {
						//	/ch/%02d/mix/%02d/pan....,f..[float]
						while (Xb_r[Xb_i] != ',') Xb_i += 1;
						Xb_i += 4;
						for (i = 4; i > 0; endian.cc[--i] = Xb_r[Xb_i++]);
						sprintf(tmp, "/track/%d/send/%d/pan", cnum + Xtrk_min - 1, bus);
						Rb_ls = Xfprint(Rb_s, 0, tmp, 'f', &endian.ff);						
					}
				}
			} else if ((Xb_r[Xb_i] == 'c') && (Xb_r[Xb_i + 7] == 'n')){
				Xb_i += 11; //skip "/config/name" string
				//	/ch/%02d/config/name..,s..[string\0]
				while (Xb_r[Xb_i] != ',') Xb_i += 1;
				Xb_i += 4;
				sprintf(tmp, "/track/%d/name", cnum + Xtrk_min - 1);
				Rb_ls = Xfprint(Rb_s, 0, tmp, 's', Xb_r + Xb_i);
			}
		}
	} else if (strncmp(Xb_r, "/rtn/aux/", 9) == 0) {
		// /rtn/aux/ special case for channel 17/18
		// MUST be BEFORE /rtn/ in else if decision
		Xb_i = 9; // skip "/rtn/aux/" string
		cnum = 1;
		if (Xb_r[Xb_i] == 'm') {
			Xb_i += 4; //skip "/mix/" string
			if (Xb_r[Xb_i] == 'p') {
				//	/rtn/aux/mix/pan......,f..[float]
				while (Xb_r[Xb_i] != ',') Xb_i += 1;
				Xb_i += 4;
				for (i = 4; i > 0; endian.cc[--i] = Xb_r[Xb_i++]);
				sprintf(tmp, "/track/%d/pan", cnum + Xaux_min - 1);
				Rb_ls = Xfprint(Rb_s, 0, tmp, 'f', &endian.ff);
			} else if (Xb_r[Xb_i] == 'f') {
				//	/rtn/aux/mix/fader....,f..[float]
				while (Xb_r[Xb_i] != ',') Xb_i += 1;
				Xb_i += 4;
				for (i = 4; i > 0; endian.cc[--i] = Xb_r[Xb_i++]);
				sprintf(tmp, "/track/%d/volume", cnum + Xaux_min - 1);
				Rb_ls = Xfprint(Rb_s, 0, tmp, 'f', &endian.ff);
			} else if (Xb_r[Xb_i] == 'o') {
				//	/rtn/aux/mix/on.......,i..[0/1]
				while (Xb_r[Xb_i] != ',') Xb_i += 1;
				Xb_i += 4;
				for (i = 4; i > 0; endian.cc[--i] = Xb_r[Xb_i++]);
				if (endian.ii == 1) endian.ff = 0.0;
				else				endian.ff = 1.0;
				sprintf(tmp, "/track/%d/mute", cnum + Xaux_min - 1);
				Rb_ls = Xfprint(Rb_s, 0, tmp, 'f', &endian.ff);
			} else if ((Xb_r[Xb_i] == '0') || (Xb_r[Xb_i] == '1')) {
				// "/mix/" is followed by a Bus send number
				// get bus number
				bus = (int) Xb_r[Xb_i++] - (int) '0';
				bus = bus * 10 + (int) Xb_r[Xb_i++] - (int) '0';
				bus += bus_offset;
				Xb_i += 1; // skip '/'
				if (Xb_r[Xb_i] == 'l') {
					//	/rtn/aux/mix/%02d/level....,f..[float]
					while (Xb_r[Xb_i] != ',') Xb_i += 1;
					Xb_i += 4;
					for (i = 4; i > 0; endian.cc[--i] = Xb_r[Xb_i++]);
					sprintf(tmp, "/track/%d/send/%d/volume", cnum + Xaux_min - 1, bus);
					Rb_ls = Xfprint(Rb_s, 0, tmp, 'f', &endian.ff);
				} else if (Xb_r[Xb_i] == 'p') {
					//	/rtn/aux/mix/%02d/pan....,f..[float]
					while (Xb_r[Xb_i] != ',') Xb_i += 1;
					Xb_i += 4;
					for (i = 4; i > 0; endian.cc[--i] = Xb_r[Xb_i++]);
					sprintf(tmp, "/track/%d/send/%d/pan", cnum + Xaux_min - 1, bus);
					Rb_ls = Xfprint(Rb_s, 0, tmp, 'f', &endian.ff);					
				}
			}
		}  else if ((Xb_r[Xb_i] == 'c') && (Xb_r[Xb_i + 7] == 'n')){
				Xb_i += 11; //skip "/config/name" string
				//	/rtn/aux/config/name..,s..[string\0]
				while (Xb_r[Xb_i] != ',') Xb_i += 1;
				Xb_i += 4;
				sprintf(tmp, "/track/%d/name", cnum + Xaux_min - 1);
				Rb_ls = Xfprint(Rb_s, 0, tmp, 's', Xb_r + Xb_i);
		}		
	}  else if (strncmp(Xb_r, "/rtn/", 5) == 0) {
		// /rtn/ cases : get channel number
		Xb_i = 5;
		cnum = (int) Xb_r[Xb_i++] - (int) '0';
		//cnum = cnum * 10 + (int) Xb_r[Xb_i++] - (int) '0';
		if ((Xfxr_max > 0) && (cnum <= (Xfxr_max - Xfxr_min + 1))) {
			Xb_i += 1; // skip '/'
			if (Xb_r[Xb_i] == 'm') {
				Xb_i += 4; //skip "/mix/" string
				if (Xb_r[Xb_i] == 'p') {
					//	/rtn/1..4/mix/pan......,f..[float]
					while (Xb_r[Xb_i] != ',') Xb_i += 1;
					Xb_i += 4;
					for (i = 4; i > 0; endian.cc[--i] = Xb_r[Xb_i++]);
					sprintf(tmp, "/track/%d/pan", cnum + Xfxr_min - 1);
					Rb_ls = Xfprint(Rb_s, 0, tmp, 'f', &endian.ff);
				} else if (Xb_r[Xb_i] == 'f') {
					//	/rtn/1..4/mix/fader....,f..[float]
					while (Xb_r[Xb_i] != ',') Xb_i += 1;
					Xb_i += 4;
					for (i = 4; i > 0; endian.cc[--i] = Xb_r[Xb_i++]);
					sprintf(tmp, "/track/%d/volume", cnum + Xfxr_min - 1);
					Rb_ls = Xfprint(Rb_s, 0, tmp, 'f', &endian.ff);
				} else if (Xb_r[Xb_i] == 'o') {
					//	/rtn/1..4/mix/on.......,i..[0/1]
					while (Xb_r[Xb_i] != ',') Xb_i += 1;
					Xb_i += 4;
					for (i = 4; i > 0; endian.cc[--i] = Xb_r[Xb_i++]);
					if (endian.ii == 1) endian.ff = 0.0;
					else				endian.ff = 1.0;
					sprintf(tmp, "/track/%d/mute", cnum + Xfxr_min - 1);
					Rb_ls = Xfprint(Rb_s, 0, tmp, 'f', &endian.ff);
				} else if ((Xb_r[Xb_i] == '0') || (Xb_r[Xb_i] == '1')) {
					// "/mix/" is followed by a Bus send number
					// get bus number
					bus = (int) Xb_r[Xb_i++] - (int) '0';
					bus = bus * 10 + (int) Xb_r[Xb_i++] - (int) '0';
					bus += bus_offset;
					Xb_i += 1; // skip '/'
					if (Xb_r[Xb_i] == 'l') {
						//	/rtn/%02d/mix/%02d/level....,f..[float]
						while (Xb_r[Xb_i] != ',') Xb_i += 1;
						Xb_i += 4;
						for (i = 4; i > 0; endian.cc[--i] = Xb_r[Xb_i++]);
						sprintf(tmp, "/track/%d/send/%d/volume", cnum + Xfxr_min - 1, bus);
						Rb_ls = Xfprint(Rb_s, 0, tmp, 'f', &endian.ff);
					} else if (Xb_r[Xb_i] == 'p') {
						//	/rtn/%02d/mix/%02d/pan....,f..[float]
						while (Xb_r[Xb_i] != ',') Xb_i += 1;
						Xb_i += 4;
						for (i = 4; i > 0; endian.cc[--i] = Xb_r[Xb_i++]);
						sprintf(tmp, "/track/%d/send/%d/pan", cnum + Xfxr_min - 1, bus);
						Rb_ls = Xfprint(Rb_s, 0, tmp, 'f', &endian.ff);						
					}
				}
			} else if ((Xb_r[Xb_i] == 'c') && (Xb_r[Xb_i + 7] == 'n')){
				Xb_i += 11; //skip "/config/name" string
				//	/rtn/%02d/config/name..,s..[string\0]
				while (Xb_r[Xb_i] != ',') Xb_i += 1;
				Xb_i += 4;
				sprintf(tmp, "/track/%d/name", cnum + Xfxr_min - 1);
				Rb_ls = Xfprint(Rb_s, 0, tmp, 's', Xb_r + Xb_i);
			}
		}
	} else if (strncmp(Xb_r, "/bus/", 5) == 0) {
		//	/bus/1..6/mix/fader....,f..[float]
		//	/bus/1..6/mix/pan......,f..[float]
		//	/bus/1..6/mix/on.......,i..[0/1]
		//	/bus/1..6/config/name..,s..[string\0]
		Xb_i = 5;
		bus = (int) Xb_r[Xb_i++] - (int) '0';
		// bus = bus * 10 + (int) Xb_r[Xb_i++] - (int) '0';
		if ((Xbus_max > 0) && (bus <= (Xbus_max - Xbus_min + 1))) {
			Xb_i += 1; // skip '/'
			if (Xb_r[Xb_i] == 'm') {
				Xb_i += 4;	// skip "mix/"
				if (Xb_r[Xb_i] == 'p') {
					//	/bus/1..6/mix/pan
					while (Xb_r[Xb_i] != ',') Xb_i += 1;
					Xb_i += 4;
					for (i = 4; i > 0; endian.cc[--i] = Xb_r[Xb_i++]);
					sprintf(tmp, "/track/%d/pan", bus + Xbus_min - 1);
					Rb_ls = Xfprint(Rb_s, 0, tmp, 'f', &endian.ff);
				} else if (Xb_r[Xb_i] == 'f') {
					//	/bus/1..6/mix/fader
					while (Xb_r[Xb_i] != ',') Xb_i += 1;
					Xb_i += 4;
					for (i = 4; i > 0; endian.cc[--i] = Xb_r[Xb_i++]);
					sprintf(tmp, "/track/%d/volume", bus + Xbus_min - 1);
					Rb_ls = Xfprint(Rb_s, 0, tmp, 'f', &endian.ff);
				} else if (Xb_r[Xb_i] == 'o') {
					//	/bus/1..6/mix/on
					while (Xb_r[Xb_i] != ',') Xb_i += 1;
					Xb_i += 4;
					for (i = 4; i > 0; endian.cc[--i] = Xb_r[Xb_i++]);
					if (endian.ii == 1) endian.ff = 0.0;
					else				endian.ff = 1.0;
					sprintf(tmp, "/track/%d/mute", bus + Xbus_min - 1);
					Rb_ls = Xfprint(Rb_s, 0, tmp, 'f', &endian.ff);
				}
			} else if ((Xb_r[Xb_i] == 'c') && (Xb_r[Xb_i + 7] == 'n')) {
				//	/bus/1..6/config/name
				Xb_i += 11; //skip "/config/name" string
				while (Xb_r[Xb_i] != ',') Xb_i += 1;
				Xb_i += 4;
				sprintf(tmp, "/track/%d/name", bus + Xbus_min - 1);
				Rb_ls = Xfprint(Rb_s, 0, tmp, 's', Xb_r + Xb_i);
			}
		}
	} else if (strncmp(Xb_r, "/dca/", 5) == 0) {
		//	/dca/1..4/on...........,i..[0/1]
		//	/dca/1..4/fader........,f..[0..1.0]
		//	/dca/1..4/config/name..,s..[string\0]
		Xb_i = 5;
		dca = (int) Xb_r[Xb_i++] - (int) '0';
		if ((Xdca_max > 0) && (dca <= (Xdca_max - Xdca_min + 1))) {
			Xb_i += 1; // skip '/'
			if (Xb_r[Xb_i] == 'f') {
				//	/dca/1..4/fader
				Xb_i += 5;	// skip "fader"
				while (Xb_r[Xb_i] != ',') Xb_i += 1;
				Xb_i += 4;
				for (i = 4; i > 0; endian.cc[--i] = Xb_r[Xb_i++]);
				sprintf(tmp, "/track/%d/volume", dca + Xdca_min - 1);
				Rb_ls = Xfprint(Rb_s, 0, tmp, 'f', &endian.ff);
			} else if (Xb_r[Xb_i] == 'o') {
				//	/dca/1..4/on
				Xb_i += 2;	// skip "on"
				while (Xb_r[Xb_i] != ',') Xb_i += 1;
				Xb_i += 4;
				for (i = 4; i > 0; endian.cc[--i] = Xb_r[Xb_i++]);
				if (endian.ii == 1) endian.ff = 0.0;
				else				endian.ff = 1.0;
				sprintf(tmp, "/track/%d/mute", dca + Xdca_min - 1);
				Rb_ls = Xfprint(Rb_s, 0, tmp, 'f', &endian.ff);
			} else if ((Xb_r[Xb_i] == 'c') && (Xb_r[Xb_i + 7] == 'n')) {
				//	/dca/1..4/config/name
				Xb_i += 11; //skip "/config/name" string
				while (Xb_r[Xb_i] != ',') Xb_i += 1;
				Xb_i += 4;
				sprintf(tmp, "/track/%d/name", dca + Xdca_min - 1);
				Rb_ls = Xfprint(Rb_s, 0, tmp, 's', Xb_r + Xb_i);
			}
		}
	} else if (strncmp(Xb_r, "/fxsend/", 8) == 0) {
		//	/fxsend/1..4/mix/on...........,i..[0/1]
		//	/fxsend/1..4/mix/fader........,f..[0..1.0]
		//	/fxsend/1..4/config/name..,s..[string\0]
		Xb_i = 8;
		fxs = (int) Xb_r[Xb_i++] - (int) '0';
		if ((Xfxs_max > 0) && (fxs <= (Xfxs_max - Xfxs_min + 1))) {
			Xb_i += 1; // skip '/'
			if (Xb_r[Xb_i] == 'm') {
				Xb_i += 4;	// skip "mix/"
				if (Xb_r[Xb_i] == 'f') {
					//	/fxsend/1..6/mix/fader
					while (Xb_r[Xb_i] != ',') Xb_i += 1;
					Xb_i += 4;
					for (i = 4; i > 0; endian.cc[--i] = Xb_r[Xb_i++]);
					sprintf(tmp, "/track/%d/volume", fxs + Xfxs_min - 1);
					Rb_ls = Xfprint(Rb_s, 0, tmp, 'f', &endian.ff);
				} else if (Xb_r[Xb_i] == 'o') {
					//	/fxsend/1..6/mix/on
					while (Xb_r[Xb_i] != ',') Xb_i += 1;
					Xb_i += 4;
					for (i = 4; i > 0; endian.cc[--i] = Xb_r[Xb_i++]);
					if (endian.ii == 1) endian.ff = 0.0;
					else				endian.ff = 1.0;
					sprintf(tmp, "/track/%d/mute", fxs + Xfxs_min - 1);
					Rb_ls = Xfprint(Rb_s, 0, tmp, 'f', &endian.ff);
				}
			} else if ((Xb_r[Xb_i] == 'c') && (Xb_r[Xb_i + 7] == 'n')) {
				//	/fxsend/1..6/config/name
				Xb_i += 11; //skip "/config/name" string
				while (Xb_r[Xb_i] != ',') Xb_i += 1;
				Xb_i += 4;
				sprintf(tmp, "/track/%d/name", fxs + Xfxs_min - 1);
				Rb_ls = Xfprint(Rb_s, 0, tmp, 's', Xb_r + Xb_i);
			}
		}
	} else if (strncmp(Xb_r, "/-stat/", 7) == 0) {
		// /-stat/ cases
		Xb_i = 10;
		if (Xb_r[Xb_i] == 'i') { // test on 'i' for selidx
			// unselect all REAPER tracks
			Rb_ls = Xsprint(Rb_s, 0, 's', "/action/40297");
			SEND_TOR(Rb_s, Rb_ls)
			Rb_ls = Xsprint(Rb_s, 0, 's', "/action/53809");
			SEND_TOR(Rb_s, Rb_ls)
			Rb_ls = 0;
			//	/-stat/selidx.........,i..[%d]
			while (Xb_r[Xb_i] != ',') Xb_i += 1;
			Xb_i += 4;
			for (i = 4; i > 0; endian.cc[--i] = Xb_r[Xb_i++]); // get track number
			cnum = -2;
			if ((endian.ii < 32) && (Xtrk_max > 0)) 			cnum = endian.ii + Xtrk_min;
			else if ((endian.ii < 40) && (Xaux_max > 0))		cnum = endian.ii + Xaux_min - 32;
			else if ((endian.ii < 48) && (Xfxr_max > 0))		cnum = endian.ii + Xfxr_min - 40;
			else if ((endian.ii < 64) && (Xbus_max > 0))		cnum = endian.ii + Xbus_min - 48;
			else if (endian.ii == 70) cnum = -1;	// set flag!
			// select requested track
			if (cnum > -2) {
				if (cnum == -1) {
					Rb_ls = Xsprint(Rb_s, 0, 's', "/action/53808");
				} else {
					sprintf(tmp, "/track/%d/select", cnum);
					Rb_ls = Xfprint(Rb_s, 0, tmp, 'f', &fone);
				}
			}
		} else if ((Xb_r[Xb_i] == 'o') && (Xb_r[Xb_i + 1] == 's')) {
			//	/-stat/solosw/%02d....,i..[0/1]
			Xb_i += 4;
			cnum = (int) Xb_r[Xb_i++] - (int) '0';
			cnum = cnum * 10 + (int) Xb_r[Xb_i++] - (int) '0';
			while (Xb_r[Xb_i] != ',') Xb_i += 1;
			Xb_i += 4;
			for (i = 4; i > 0; endian.cc[--i] = Xb_r[Xb_i++]);
			if (endian.ii == 1) endian.ff = 1.0;
			else				endian.ff = 0.0;
			i = 0;
			// XXX: Needs Review // Assumes tracks are in /-stat/solosw order
			if (cnum == 50) { // For now handle /master/solo here
				sprintf(tmp, "/master/solo");
			} else {
				sprintf(tmp, "/track/%d/solo", cnum);
			}
			// !! TODO find Master track REAPER numbering
			Rb_ls = Xfprint(Rb_s, 0, tmp, 'f', &endian.ff);
		}
	} else if (strncmp(Xb_r, "/lr/", 4) == 0) {
		if(Xmaster_on) {
		//	/lr cases
			Xb_i += 4;
			if (Xb_r[Xb_i] == 'm') {
				Xb_i += 4; //skip "/mix/" string
				if (Xb_r[Xb_i] == 'p') {
					//	/lr/mix/pan......,f..[float]
					while (Xb_r[Xb_i] != ',') Xb_i += 1;
					Xb_i += 4;
					for (i = 4; i > 0; endian.cc[--i] = Xb_r[Xb_i++]);
					sprintf(tmp, "/master/pan");
					Rb_ls = Xfprint(Rb_s, 0, tmp, 'f', &endian.ff);
				} else if (Xb_r[Xb_i] == 'f') {
					//	/lr/mix/fader....,f..[float]
					while (Xb_r[Xb_i] != ',') Xb_i += 1;
					Xb_i += 4;
					for (i = 4; i > 0; endian.cc[--i] = Xb_r[Xb_i++]);
					sprintf(tmp, "/master/volume");
					Rb_ls = Xfprint(Rb_s, 0, tmp, 'f', &endian.ff);
				} else if (Xb_r[Xb_i] == 'o') {
					//	/lr/mix/on.......,i..[0/1]
					while (Xb_r[Xb_i] != ',') Xb_i += 1;
					Xb_i += 4;
					for (i = 4; i > 0; endian.cc[--i] = Xb_r[Xb_i++]);
					if (endian.ii == 1) endian.ff = 0.0;
					else				endian.ff = 1.0;
					sprintf(tmp, "/master/mute");
					Rb_ls = Xfprint(Rb_s, 0, tmp, 'f', &endian.ff);
				}
			} else if ((Xb_r[Xb_i] == 'c') && (Xb_r[Xb_i + 7] == 'n')) {
				//	/lr/config/name..,s..[string\0]
				Xb_i += 11; //skip "/config/name" string
				while (Xb_r[Xb_i] != ',') Xb_i += 1;
				Xb_i += 4;
				sprintf(tmp, "/master/name");
				Rb_ls = Xfprint(Rb_s, 0, tmp, 's', Xb_r + Xb_i);				
			}
		}
	} else if (strncmp(Xb_r, "/-snap/", 7) == 0) {
		// /-snap/index ,i and /-snap/name ,s 
		Xb_i = 7;
		if (Xb_r[Xb_i] == 'i') { // test on 'i' in xx/index
	        //skip index
			Xb_i += 5;
			while (Xb_r[Xb_i] != ',') Xb_i += 1;
			Xb_i += 4;
			for (i = 4; i > 0; endian.cc[--i] = Xb_r[Xb_i++]);
			sprintf(tmp, "/snap/index");
			// i = (int) endian.ff;
			Rb_ls = Xfprint(Rb_s, 0, tmp, 'i', &endian.ii);
		} else if (Xb_r[Xb_i] == 'n') { // test on 'n' in xx/name
	        //skip name
			Xb_i += 4;
			while (Xb_r[Xb_i] != ',') Xb_i += 1;
			Xb_i += 4;
			sprintf(tmp, "/snap/name");
			Rb_ls = Xfprint(Rb_s, 0, tmp, 's', Xb_r + Xb_i);
		}
	}
	if (Rb_ls) {
		SEND_TOR(Rb_s, Rb_ls)
		Rb_ls = 0; // REMOTE message has been sent
	}
	return (Rb_ls);
}
//--------------------------------------------------------------------
// REMOTE Messages data parsing
//
//
int XAirParseRemoteMessage() {

	int Rb_i = 0;
	int Xb_ls = 0;
	int bundle = 0;
	int Rb_nm = 0;
	int mes_len, tnum, bus;
	int i;
	char tmp[32];
//
// is the message a bundle ?
	if (strncmp(Rb_r, "#bundle", 7) == 0) {
		bundle = 1;
		Rb_i = 16;
	}
//	Xlogf("Remote: ", Rb_r, Rb_lr) ; fflush(log_file);
	do {
		if (bundle) {
			mes_len = (int)Rb_r[Rb_i + 2] * 256 + (int)Rb_r[Rb_i + 3]; // sub-message length fits on 2 bytes max
			Rb_i += 4;
			if ((Rb_nm = (Rb_i + mes_len)) >= Rb_lr) bundle = 0; // Rb_nm is used later!
		}
		// Parse message or message parts
		Xb_ls = 0;
		if (strncmp(Rb_r + Rb_i, "/track/", 7) == 0) {
			Rb_i += 7;
			// build track number...
			tnum = (int) Rb_r[Rb_i++] - (int) '0';
			while (Rb_r[Rb_i] != '/') tnum = tnum * 10 + (int) Rb_r[Rb_i++] - (int) '0';
			Rb_i++; // skip '/'
			// Got track #
			// Known: /pan, /volume, /name, /mute, /solo, /send
			if (Rb_r[Rb_i] == 'p') { // /track/xx/pan
				while (Rb_r[Rb_i] != ',') Rb_i += 1;
				Rb_i += 4;
				for (i = 4; i > 0; endian.cc[--i] = Rb_r[Rb_i++]);
//					/track/[1-N]/mix/pan ,f -100 100
				if ((tnum >= Xtrk_min) && (tnum <= Xtrk_max))  		sprintf(tmp, "/ch/%02d/mix/pan", tnum  - Xtrk_min + 1);
				else if ((tnum >= Xaux_min) && (tnum <= Xaux_max))	sprintf(tmp, "/rtn/aux/mix/pan");
				else if ((tnum >= Xfxr_min) && (tnum <= Xfxr_max))	sprintf(tmp, "/rtn/%1d/mix/pan", tnum - Xfxr_min + 1);
				else if ((tnum >= Xbus_min) && (tnum <= Xbus_max))	sprintf(tmp, "/bus/%1d/mix/pan", tnum - Xbus_min + 1);
				else tnum = -1;
				if (tnum > 0) Xb_ls = Xfprint(Xb_s, 0, tmp, 'f', &endian.ff);
			} else if (Rb_r[Rb_i] == 'v') { // /track/xx/volume
				while (Rb_r[Rb_i] != ',') Rb_i += 1;
				Rb_i += 4;
				for (i = 4; i > 0; endian.cc[--i] = Rb_r[Rb_i++]);
// 				Make REMOTE stick to XAir known values to avoid fader kick-backs
				endian.ff = roundf(endian.ff * 1023.) / 1023.;
//				sprintf(tmp, "/track/%d/volume", tnum);
//				Rb_ls = Xfprint(Rb_s, 0, tmp, 'f', &endian.ff);
//				SEND_TOR(Rb_s, Rb_ls)
//				Rb_ls = 0; // REMOTE message has been sent
//
// !! take this back. Doing the above creates issues when several REAPER tracks are simultaneously selected.
// REAPER buffers the changes for the other tracks and applies them after the changes on one of the Tracks
// are done. This results in an infinite loop. :(
//
//					/track/[1-N]/mix/fader ,f 0..1
				if ((tnum >= Xtrk_min) && (tnum <= Xtrk_max))		sprintf(tmp, "/ch/%02d/mix/fader", tnum - Xtrk_min + 1);
				else if ((tnum >= Xaux_min) && (tnum <= Xaux_max))	sprintf(tmp, "/rtn/aux/mix/fader");
				else if ((tnum >= Xfxr_min) && (tnum <= Xfxr_max))	sprintf(tmp, "/rtn/%1d/mix/fader", tnum - Xfxr_min + 1);
				else if ((tnum >= Xbus_min) && (tnum <= Xbus_max))	sprintf(tmp, "/bus/%1d/mix/fader", tnum - Xbus_min + 1);
				else if ((tnum >= Xfxs_min) && (tnum <= Xfxs_max))	sprintf(tmp, "/fxsend/%1d/mix/fader", tnum - Xfxs_min + 1);
				else if ((tnum >= Xdca_min) && (tnum <= Xdca_max))	sprintf(tmp, "/dca/%1d/fader", tnum - Xdca_min + 1);
				else tnum = -1;
				if (tnum > 0) Xb_ls = Xfprint(Xb_s, 0, tmp, 'f', &endian.ff);
			} else if (Rb_r[Rb_i] == 'n') { // /track/xx/name
				while (Rb_r[Rb_i] != ',') Rb_i += 1;
				Rb_i += 4;
//					/track/[1-N]/config/name ,s string
				if ((tnum >= Xtrk_min) && (tnum <= Xtrk_max))		sprintf(tmp, "/ch/%02d/config/name", tnum - Xtrk_min + 1);
				else if ((tnum >= Xaux_min) && (tnum <= Xaux_max))	sprintf(tmp, "/rtn/aux/config/name");
				else if ((tnum >= Xfxr_min) && (tnum <= Xfxr_max))	sprintf(tmp, "/rtn/%1d/config/name", tnum - Xfxr_min + 1);
				else if ((tnum >= Xbus_min) && (tnum <= Xbus_max))	sprintf(tmp, "/bus/%1d/config/name", tnum - Xbus_min + 1);
				else if ((tnum >= Xfxs_min) && (tnum <= Xfxs_max))	sprintf(tmp, "/fxsend/%1d/config/name", tnum - Xfxs_min + 1);
				else if ((tnum >= Xdca_min) && (tnum <= Xdca_max))	sprintf(tmp, "/dca/%1d/config/name", tnum - Xdca_min + 1);
				else tnum = -1;
				if (tnum > 0) {
					Xb_ls = Xfprint(Xb_s, 0, tmp, 's', Rb_r + Rb_i);
				}
			} else if (Rb_r[Rb_i] == 'm') { // /track/xx/mute
				while (Rb_r[Rb_i] != ',') Rb_i += 1;
				Rb_i += 4;
				for (i = 4; i > 0; endian.cc[--i] = Rb_r[Rb_i++]);
				i = 1 - (int) endian.ff;
//					/track/[1-N]/mix/on ,i 0/1
				if ((tnum >= Xtrk_min) && (tnum <= Xtrk_max))		sprintf(tmp, "/ch/%02d/mix/on", tnum - Xtrk_min + 1);
				else if ((tnum >= Xaux_min) && (tnum <= Xaux_max))	sprintf(tmp, "/rtn/aux/mix/on");
				else if ((tnum >= Xfxr_min) && (tnum <= Xfxr_max))	sprintf(tmp, "/rtn/%1d/mix/on", tnum - Xfxr_min + 1);
				else if ((tnum >= Xbus_min) && (tnum <= Xbus_max))	sprintf(tmp, "/bus/%1d/mix/on", tnum - Xbus_min + 1);
				else if ((tnum >= Xfxs_min) && (tnum <= Xfxs_max))	sprintf(tmp, "/fxsend/%1d/mix/on", tnum - Xfxs_min + 1);
				else if ((tnum >= Xdca_min) && (tnum <= Xdca_max))	sprintf(tmp, "/dca/%1d/on", tnum - Xdca_min + 1);
				else tnum = -1;
				if (tnum > 0) Xb_ls = Xfprint(Xb_s, 0, tmp, 'i', &i);
			} else if (Rb_r[Rb_i] == 's') { // /track/xx/send or /track/xx/solo
				Rb_i++;
				if ((Rb_r[Rb_i] == 'e') && (Rb_r[Rb_i + 1] == 'n')) { // /track/send
					// example: /track/6/send/2/volume\0\0,f\0\0?7KÇ (track 6 sending to 2nd bus)
					Rb_i += 4;	// skip "....send/"
					// build bus track number
					bus = (int) Rb_r[Rb_i++] - (int) '0';
					while (Rb_r[Rb_i] != '/')
						bus = bus * 10 + (int) Rb_r[Rb_i++] - (int) '0';
					bus -= bus_offset;
					Rb_i++; // skip '/'
					if (Rb_r[Rb_i] == 'v') { // volume <float>
						while (Rb_r[Rb_i] != ',') Rb_i += 1;
						Rb_i += 4;
						for (i = 4; i > 0; endian.cc[--i] = Rb_r[Rb_i++]);
						// /track/<tnum>/mix/<bus>/level ,f 0...1.
						if ((tnum >= Xtrk_min) && (tnum <= Xtrk_max))		sprintf(tmp, "/ch/%02d/mix/%02d/level", tnum - Xtrk_min + 1, bus);
						else if ((tnum >= Xaux_min) && (tnum <= Xaux_max))	sprintf(tmp, "/rtn/aux/mix/%02d/level", bus);
						else if ((tnum >= Xfxr_min) && (tnum <= Xfxr_max))	sprintf(tmp, "/rtn/%1d/mix/%02d/level", tnum - Xfxr_min + 1, bus);
						else tnum = -1;
						if (tnum > 0) Xb_ls = Xfprint(Xb_s, 0, tmp, 'f', &endian.ff);
					} else if (Rb_r[Rb_i] == 'p') { // pan <float>
						while (Rb_r[Rb_i] != ',') Rb_i += 1;
						Rb_i += 4;
						for (i = 4; i > 0; endian.cc[--i] = Rb_r[Rb_i++]);
						// /track/<tnum>/mix/<bus>/pan ,f 0...1.
						if ((tnum >= Xtrk_min) && (tnum <= Xtrk_max))		sprintf(tmp, "/ch/%02d/mix/%02d/pan", tnum - Xtrk_min + 1, bus);
						else if ((tnum >= Xaux_min) && (tnum <= Xaux_max))	sprintf(tmp, "/rtn/aux/mix/%02d/pan", bus);
						else if ((tnum >= Xfxr_min) && (tnum <= Xfxr_max))	sprintf(tmp, "/rtn/%1d/mix/%02d/pan", tnum - Xfxr_min + 1, bus);
						else tnum = -1;
						if (tnum > 0) Xb_ls = Xfprint(Xb_s, 0, tmp, 'f', &endian.ff);
					}
				// XXX: Needs Review. This assumes /-stat/solosw track ordering
				} else if (Rb_r[Rb_i] == 'o') { // /track/xx/solo
					while (Rb_r[Rb_i] != ',') Rb_i += 1;
					Rb_i += 4;
					for (i = 4; i > 0; endian.cc[--i] = Rb_r[Rb_i++]);
					i = (int) endian.ff;
					// set XAir Channel number based on track
					if (!(tnum >= Xtrk_min) && (tnum <= Xdca_max) && (tnum != 50)) tnum = -1;
					if (tnum > 0) {// TODO: How to select Master from REAPER???
						sprintf(tmp, "/-stat/solosw/%02d", tnum);
						Xb_ls = Xfprint(Xb_s, 0, tmp, 'i', &i);
					}
				}
			}
		} 	else if (strncmp(Rb_r + Rb_i, "/master/", 8) == 0) {
			if(Xmaster_on) {
			// MASTER
				Rb_i += 8;
				// Known: /master/name, /master/pan, /master/volume, /master/solo, /master/mute
				// TODO: Not possible today: select
				if (Rb_r[Rb_i] == 'p') { // pan
					while (Rb_r[Rb_i] != ',') Rb_i += 1;
					Rb_i += 4;
					for (i = 4; i > 0; endian.cc[--i] = Rb_r[Rb_i++]);
					Xb_ls = Xfprint(Xb_s, 0, "/lr/mix/pan", 'f', &endian.ff);
				}
				if (Rb_r[Rb_i] == 'v') { // volume
					while (Rb_r[Rb_i] != ',') Rb_i += 1;
					Rb_i += 4;
					for (i = 4; i > 0; endian.cc[--i] = Rb_r[Rb_i++]);
					Xb_ls = Xfprint(Xb_s, 0, "/lr/mix/fader", 'f', &endian.ff);
				}
				if (Rb_r[Rb_i] == 'm') { // mute
					while (Rb_r[Rb_i] != ',') Rb_i += 1;
					Rb_i += 4;
					for (i = 4; i > 0; endian.cc[--i] = Rb_r[Rb_i++]);
					i = 1 - (int) endian.ff;
					Xb_ls = Xfprint(Xb_s, 0, "/lr/mix/on", 'i', &i);
				}
				if (Rb_r[Rb_i] == 's') { // solo 
					while (Rb_r[Rb_i] != ',') Rb_i += 1;
					Rb_i += 4;
					for (i = 4; i > 0; endian.cc[--i] = Rb_r[Rb_i++]);
					// XXX: Needs Review. '50' is LR in /-stat/solosw track ordering
					Xb_ls = Xfprint(Xb_s, 0, "/-stat/solosw/50", 'f', &endian.ff); 
				}
				if (Rb_r[Rb_i] == 'n') { // name
					while (Rb_r[Rb_i] != ',') Rb_i += 1;
					Rb_i += 4;
					Xb_ls = Xfprint(Xb_s, 0, "/lr/config/name", 's', Rb_r + Rb_i);
				}
			} 
		}	else if (strncmp(Rb_r + Rb_i, "/snap/", 6) == 0) {	
				Rb_i += 6;
                if (Rb_r[Rb_i] == 'l') { // /snap/load
				    while (Rb_r[Rb_i] != ',') Rb_i += 1;
				    Rb_i += 4;
				    for (i = 4; i > 0; endian.cc[--i] = Rb_r[Rb_i++]);
				    i = (int) endian.ff;
				    // Don't do anything for snapshot 0 (button release)
				    // printf("i = %d\n", i);
				    if (( i > 0 ) && ( i <= 64 )) { // Only trigger when value is valid
				    	Xb_ls = Xfprint(Xb_s, 0, "/-snap/load", 'i', &i);
				    }
				} else if (Rb_r[Rb_i] == 'i') { // /snap/info
				    while (Rb_r[Rb_i] != ',') Rb_i += 1;
				    Rb_i += 4;
				    for (i = 4; i > 0; endian.cc[--i] = Rb_r[Rb_i++]);
				    if ((int) endian.ff) { // Only trigger when value != 0
				        // Send messages directly
						Xb_ls = Xsprint(Xb_s, 0, 's', "/-snap/index");
						if (Xb_ls) SEND_TOX(Xb_s, Xb_ls)
				        Xb_ls = Xsprint(Xb_s, 0, 's', "/-snap/name");
						if (Xb_ls) SEND_TOX(Xb_s, Xb_ls)
						Xb_ls = 0; // Don't pass on to the XAir. 
					}
				}
			} else if (strncmp(Rb_r + Rb_i, "/sync", 5) == 0) {	
//	This is a special OSC message from the REMOTE that triggers a parameter sync from the XAir.
//      Mimics X-AIR-Edit Mixer->PC synchronization. 
				Rb_i += 5;
				while (Rb_r[Rb_i] != ',') Rb_i += 1;
				Rb_i += 4;
				for (i = 4; i > 0; endian.cc[--i] = Rb_r[Rb_i++]);
				// This is to avoid a double sync when a toggle button is pushed and released. 
				if ((int) endian.ff) { // Only trigger when value !=0
				    Xsync_on = 1; // Enable Sync starting next loop
				    Xb_ls = 0; // Don't pass on to the XAir. 
				}
			}
		if (Xb_ls) SEND_TOX(Xb_s, Xb_ls)
		Rb_i = Rb_nm; // Set Rb_ls pointing to next message (at index Rb_nm) in Reaper bundle
	} while (bundle);
	return (Xb_ls);
}



