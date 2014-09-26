// hello_client.c --- A simple demo of Rick van Rein's SRV library.
//
// (c) 2001 Rick van Rein, see README for licensing terms.
//
// Find the library at http://dns.vanrein.org/srv/lib
//
// This protocol awaits connections and receives a name from them, to which
// it responds by greeting the other side and mentioning its own name.


#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>

#include <arpa/srvname.h>

int main (int argc, char *argv []) {
	int sox;
	int len;
	char msg [101];

	// Verify/report cmdline usage
	if (argc != 3) {
		fprintf (stderr,
			"Usage:   %s domain yourname\n"
			"\n"
			"Example: %s dns.vanrein.org John\n"
			"Matches: _hello._tcp.dns.vanrein.org. IN SRV ...\n",
				argv [0],
				argv [0]
			);
		exit (1);
	}

	// Setup the connection, report any trouble
	sox = insrv_client ("hello", "tcp", argv [1], NULL, 0, NULL);
perror ("No error I guess");
	if (sox <= 0) {
		perror ("Failed to connect client");
		exit (1);
	}

	// Start sending and receiving data over the connection
	if (send (sox, argv [2], strlen (argv [2]), 0) >= 0) {
		while (len = recv (sox, msg, 100, 0)) {
			if (len < 0) {
				perror ("Error receiving data");
				shutdown (sox, 2);
				close (sox);
				exit (1);
			}
			msg [len]='\0';
			printf ("%s", msg);
		}
	} else {
		perror ("Failure to send greeting");
	}

	// Properly shutdown the client socket
	shutdown (sox, 2);
	close (sox);
	exit (0);
}
