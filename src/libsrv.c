/* $Id: libsrv.c,v 1.4 2002/10/02 13:28:11 vanrein Exp $
 *
 * libsrv.c --- Make a connection to a service described with SRV records.
 *
 * A simple library to quickly allocate a socket on either side of a network
 * connection.  Much simpler than classical sockets, and much better too.
 * Tries IN SRV records first, continues to IN A if none defined.
 *
 * By default, it even forks a new server process for every server connection!
 * This can be overridden, and manual accept() and handling can then be done.
 * Do not expect too much -- for a more complex solution, consider RULI,
 *	http://ruli.sourceforge.net
 *
 * From: Rick van Rein <admin@FingerHosting.com>
 */


/* EXAMPLE USE:
 *
 *	In your DNS config, setup SRV records for a service, say:
 *		_hello._tcp.vanrein.org IN SRV 1234 0 0 bakkerij.orvelte.nep.
 *	Meaning:
 *	 - The hello protocol runs over tcp
 *	 - The server is on port 1234 of host bakkerij.orvelte.nep
 *	 - The zeroes are for load balancing
 *
 *	A hello client starting for this domain looks up the above record and
 *	returns a socket of a network client when invoked as follows:
 *		insrv_client ("finger", "tcp", "vanrein.org");
 *
 *	A hello server started on the server host looks up the same record
 *	and starts service for hello over TCP when it finds one of its names
 *	in the record; a client-connected socket is returned from:
 *		insrv_server ("finger", "tcp", "vanrein.org");
 *	Unless told otherwise, this routine forks off for every connection
 *	to the server.  So, the server can process the request on the
 *	returned socket, close it and exit.
 *
 *	When both calls have completed, they are connected and can start
 *	running the Jabber protocol over their respective sockets.
 */

#define BIND_4_COMPAT

#include <arpa/srvname.h>

#include <sys/types.h>
#include <sys/wait.h>
#inlcude <sys/time.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>
#include <arpa/nameser.h>
#include <resolv.h>
#include <errno.h>
#include <stdlib.h>
#include <netdb.h>
#include <sys/socket.h>
#include <string.h>
#include <signal.h>
#include <syslog.h>


/* Common offsets into an SRV RR */
#define SRV_COST    (RRFIXEDSZ+0)
#define SRV_WEIGHT  (RRFIXEDSZ+2)
#define SRV_PORT    (RRFIXEDSZ+4)
#define SRV_SERVER  (RRFIXEDSZ+6)
#define SRV_FIXEDSZ (RRFIXEDSZ+6)


/* Data structures */
typedef struct {
	unsigned char buf [PACKETSZ];
	int len;
} iobuf;
typedef unsigned char name [MAXDNAME];
#define MAXNUM_SRV PACKETSZ


/* Global variable for #listeners (no OS implements it, but let's play along) */
int _listen_nr = 10;

/* Local variable for SRV options */
static unsigned long int srv_flags = 0L;


/* Setup the SRV options when initialising -- invocation optional */
void insrv_init (unsigned long flags) {
	srv_flags = flags;
	res_init ();
}


/* Test the given SRV options to see if all are set */
int srv_testflag (unsigned long flags) {
	return ((srv_flags & flags) == flags) ? 1 : 0;
}


/* Compare two SRV records by priority and by (scattered) weight */
static int srvcmp (const void *left, const void *right) {
	int lcost = ntohs (((unsigned short **) left ) [0][5]);
	int rcost = ntohs (((unsigned short **) right) [0][5]);
	if (lcost == rcost) {
		lcost = -ntohs (((unsigned short **) left ) [0][6]);
		rcost = -ntohs (((unsigned short **) right) [0][6]);
	}
	if (lcost < rcost) {
		return -1;
	} else if (lcost > rcost) {
		return +1;
	} else {
		return  0;
	}
}


/* Setup a client socket for the named service over the given protocol under
 * the given domain name.
 */
static int insrv_lookup (int (*mksox) (int,const struct sockaddr *,socklen_t),
			char *service, char *proto, char *domain,
			char *cnxhost, size_t cnxhlen, int *cnxport) {
	// 1. convert service/proto to svcnm
	// 2. construct SRV query for _service._proto.domain
	// 3. try connecting to all answers in turn
	// 4. if no SRV records exist, lookup A record to connect to on stdport
	// 5. return connection socket or error code

	iobuf query, names;
	name svcnm;
	int error=0;
	int ctr;
	int rnd;
	int sox=0;
	HEADER *nameshdr;
	unsigned char *here, *srv[MAXNUM_SRV], *ip;
	int num_srv=0;
	// Storage for fallback SRV list, constructed when DNS gives no SRV
	unsigned char fallbacksrv [2*(MAXCDNAME+SRV_FIXEDSZ+MAXCDNAME)];

	srv_flags &= ~SRV_GOT_MASK;
	srv_flags |=  SRV_GOT_SRV;

	strcpy (svcnm, "_");
	strcat (svcnm, service);
	strcat (svcnm, "._");
	strcat (svcnm, proto);

	// Note that SRV records are only defined for class IN
	if (domain) {
		names.len=res_querydomain (svcnm, domain,
				C_IN, T_SRV,
				names.buf, PACKETSZ);
	} else {
		names.len=res_query (svcnm,
				C_IN, T_SRV,
				names.buf, PACKETSZ);
	}
	if (names.len < 0) {
		error = -ENOENT;
		goto fallback;
	}
	nameshdr=(HEADER *) names.buf;
	here=names.buf + HFIXEDSZ;
	rnd=nameshdr->id; 	// Heck, gimme one reason why not!

	if ((names.len < HFIXEDSZ) || nameshdr->tc) {
		error = -EMSGSIZE;
	}
	switch (nameshdr->rcode) {
		case 1:
			error = -EFAULT;
			goto fallback;
		case 2:
			error = -EAGAIN;
			goto fallback;
		case 3:
			error = -ENOENT;
			goto fallback;
		case 4:
			error = -ENOSYS;
			goto fallback;
		case 5:
			error = -EPERM;
			goto fallback;
		default:
			break;
	}
	if (ntohs (nameshdr->ancount) == 0) {
		error = -ENOENT;
		goto fallback;
	}
	if (ntohs (nameshdr->ancount) > MAXNUM_SRV) {
		error = -ERANGE;
		goto fallback;
	}
	for (ctr=ntohs (nameshdr->qdcount); ctr>0; ctr--) {
		int strlen=dn_skipname (here, names.buf+names.len);
		here += strlen + QFIXEDSZ;
	}
	for (ctr=ntohs (nameshdr->ancount); ctr>0; ctr--) {
		int strlen=dn_skipname (here, names.buf+names.len);
		here += strlen;
		srv [num_srv++] = here;
		here += SRV_FIXEDSZ;
		here += dn_skipname (here, names.buf+names.len);
	}

	// In case an error occurred, there are no SRV records.
	// Fallback strategy now is: construct two. One with the domain name,
	// the other with the /standard/ service name prefixed.
	// Note: Assuming a domain without the service name prefixed!
fallback:
	if (error) {
		struct servent *servent = getservbyname (service, proto);

		srv_flags &= ~SRV_GOT_MASK;
		srv_flags |=  SRV_GOT_A;

		num_srv = 2;
		if (!servent) {
			return error; // First error returned
		}
		srv [0] = here = fallbacksrv;
		// Only few record fields are really needed:
		*(unsigned short *)(here + SRV_COST)   = htons (0);
		*(unsigned short *)(here + SRV_WEIGHT) = htons (0);
		*(unsigned short *)(here + SRV_PORT)   = servent->s_port;
		here += SRV_FIXEDSZ;
		if (domain) {
			here += dn_comp (domain, here, MAXCDNAME, NULL, NULL);
		}
		// Forget about the name whose SRV IN this is, no need for it
		srv [1] = here;
		// Only few record fields are really needed:
		*(unsigned short *)(here + SRV_COST)   = htons (1);
		*(unsigned short *)(here + SRV_WEIGHT) = htons (0);
		*(unsigned short *)(here + SRV_PORT)   = servent->s_port;
		here += SRV_FIXEDSZ;
		here += dn_comp (servent->s_name, here, MAXCDNAME, NULL, NULL);
		here--; // Go back to overwrite final zero byte
		if (domain) {
			here += dn_comp (domain, here, MAXCDNAME, NULL, NULL);
		}
		rnd = 1;
	}
	// End of fallback construction, making sure that variables are defined
	// srv[] points to the SRV RR, num_srv counts the number of entries.
	// Every SRV RR has at least cost, weight, port and servername set.
	
#ifdef DEBUG
	for (ctr=0; ctr<num_srv; ctr++) {
		name srvname;
		if (ns_name_ntop (srv [ctr]+SRV_SERVER, srvname, MAXDNAME) < 0) {
			return -errno;
		}
		printf ("Considering SRV server %d %d %d\t%s\n",
				ns_get16 (srv [ctr]+SRV_COST),
				ns_get16 (srv [ctr]+SRV_WEIGHT),
				ns_get16 (srv [ctr]+SRV_PORT),
				srvname
			);
	}
#endif

	// Overwrite weight with rnd-spread version to divide load over weights
	for (ctr=0; ctr<num_srv; ctr++) {
		*(unsigned short *)(srv [ctr]+SRV_WEIGHT)
			= rnd % (1+ns_get16 (srv [ctr]+SRV_WEIGHT));
	}
	qsort (srv, num_srv, sizeof (*srv), srvcmp);


	for (ctr=0; ctr<num_srv; ctr++) {
		name srvname;
		struct hostent *host;
		// Open a socket to connect with
		int sox = socket (PF_INET, (*proto!='u')? SOCK_STREAM: SOCK_DGRAM, 0);
		if (sox < 0) {
			return -errno;
		}
		if (ns_name_ntop (srv [ctr]+SRV_SERVER, srvname, MAXDNAME) < 0) {
			return -errno;
		}
#ifdef DEBUG
		printf ("Trying SRV server %d %d\t%s\n",
				ns_get16 (srv [ctr]+SRV_COST),
				*(unsigned short *)(srv [ctr]+SRV_WEIGHT),
				srvname
			);
#endif
		if ((host=gethostbyname (srvname))
				&& (host->h_addrtype == AF_INET)) {
			char **ip=host->h_addr_list;
			while (*ip) {
				char *ipnr=*ip;
				struct sockaddr_in sin;
				memset (&sin, 0, sizeof (sin));
				sin.sin_family = AF_INET;
				memcpy (&sin.sin_addr,
						ipnr,
						sizeof (sin.sin_addr));
				sin.sin_port = *(unsigned short *) (srv [ctr]+SRV_PORT);
#ifdef DEBUG
				fprintf (stderr, "\tbind_connect (%d, 0x%08lx, %d)\t",
					sox,
					ntohl(*(unsigned long *)ipnr),
					ntohs (sin.sin_port));
#endif
#ifdef DEBUG
				if (mksox == connect) {
					printf ("mksox == connect\n");
				}
				if (mksox == bind) {
					printf ("mksox == bind\n");
				}
				{ int i; printf ("SIN ="); for (i=0; i<sizeof (sin); i++) printf (" %02x", (int) (((unsigned char *) &sin) [i])); printf ("\n"); }
#endif
				if (mksox (sox, (struct sockaddr *) &sin, sizeof (sin)) == 0) {
#ifdef DEBUG
					fprintf (stderr, "Connected or bound to %s:%d\n", srvname, ntohs (sin.sin_port));
#endif
					if (cnxhost) {
						if (strlen (cnxhost) > cnxhlen-1) {
							*cnxhost = '\0';
						} else {
							strncpy (cnxhost,srvname,cnxhlen);
						}
					}
					if (cnxport) {
						*cnxport = ntohs (sin.sin_port);
					}
					return sox;
				} else {
					if (!error) {
						error = -errno;
					}
				}
				ip++;
			}
			
		}
#ifdef DEBUG
		printf ("Closing socket %d\n", sox);
#endif
		close (sox);
	}

	if (!error) {
		error = -ENOENT;
	}
	return error;

}




/* Setup a client socket for the named service over the given protocol under
 * the given domain name.
 */
int insrv_client (char *service, char *proto, char *domain,
			char *cnxhost, size_t cnxhlen, int *cnxport) {
	int sox = insrv_lookup (connect,
					service, proto, domain,
					cnxhost, cnxhlen, cnxport);
	return sox;
}

/* The insrv_cleanup routine waits for the termination of all child processes,
 * and then calls the old handler -- it is a signal handler for SIGINT.
 */
static int offspring = 0;
static int ismaster = 1;
static void (*oldsigint) (int);
static void sigint (int signum) {
	int status;
	while (offspring > 0) {
		wait (&status);
#ifdef DEBUG
		printf ("A zombie dropped dead on the floor\n");
#endif
		offspring--;
	}
	signal (SIGINT, oldsigint);
	kill (getpid (), SIGINT);
}



/* Stop the process of shooting loose zombies once a minute.
 */
void stop_shooting_zombies (void) {
	struct itimerval zombie_shooting_schedule;
#ifdef DEBUG
	printf ("Ending zombie-shooting timer\n");
#endif
	zombie_shooting_schedule.it_interval.tv_sec = 0;
	zombie_shooting_schedule.it_interval.tv_usec = 0;
	zombie_shooting_schedule.it_value.tv_sec = 0;
	zombie_shooting_schedule.it_value.tv_usec = 0;
	if (setitimer (ITIMER_REAL, &zombie_shooting_schedule, NULL)) {
		; // Who cares?  It cannot do harm, can it?
	}
}

/* Start the process of shooting loose zombies once a minute.
 * This is called upon the start of a first subprocess, and it ends itself
 * when the last subprocess ends.
 */
void start_shooting_zombies (void) {
	struct itimerval zombie_shooting_schedule;
#ifdef DEBUG
	printf ("Starting zombie-shooting timer\n");
#endif
	zombie_shooting_schedule.it_interval.tv_sec = 15;
	zombie_shooting_schedule.it_interval.tv_usec = 0;
	zombie_shooting_schedule.it_value.tv_sec = 15;
	zombie_shooting_schedule.it_value.tv_usec = 0;
	if (setitimer (ITIMER_REAL, &zombie_shooting_schedule, NULL)) {
		syslog (LOG_ERR, "Failed to start zombie shooting scheduler");
	}
}


/* Cleanup any zombie processes that have terminated. 
 * This operation is scheduled once a minute, until the last subprocess has
 * been ended, in which case this signal handler stops the interrupt timer
 * that schedules its re-invocation.
 */
void shoot_zombies (int cause) {
	pid_t sub;
	int status;
	do {
		sub = waitpid (-1, &status, WNOHANG);
		if (sub > 0) {
			offspring--;
#ifdef DEBUG
			printf (" ** BANG ** I just shot a zombie out of the sky -- %d remaining!\n", offspring);
#endif
			if (offspring == 0) {
				stop_shooting_zombies ();
			}
		}
	} while ((sub != 0) && (offspring > 0));
}


/* Setup a server socket for the named service over the given protocol under
 * the given domain name.
 *
 * This is a very powerful function, a complete server for not too heavy
 * protocols.  It forks for every connection setup by a client, returning
 * the connected socket.  Meanwhile, the parent process continues to listen
 * for more connection requests.
 *
 * Errors are fatal, and cause invocation of exit().
 */
int insrv_server (char *service, char *proto, char *domain,
			char *cnxhost, size_t cnxhlen, int *cnxport) {
	int sox = 1;
	int cnx;
	int nr = 0;
	struct itimerval zombie_shooting_timer;

	// Cause ^C handling to wait for ending clients first
	offspring = 0;
	ismaster = 1;
	oldsigint = signal (SIGINT, sigint);

	// Prepare for the regular cleanup of terminated child processes
	// Do not cause SIGALRM yet -- want until a client connects
	signal (SIGALRM, shoot_zombies);
	

	while (sox >= 0) {
		sox = insrv_lookup (bind,
					service, proto, domain,
					cnxhost, cnxhlen, cnxport);
		if (sox >= 0) {
			nr++;
			listen (sox, _listen_nr);
			if (srv_flags & SRV_NO_ACCEPT) {
				return sox;
			}
			while (cnx = accept (sox, NULL, 0)) {
				if (cnx < 0) {
					return -errno;
				}
				if (fork () == 0) {
					ismaster = 0;
					return cnx;
				} else {
					if (!offspring++) {
						start_shooting_zombies ();
					}
#ifdef DEBUG
					printf ("I just created a will-be zombie process -- now counting %d\n", offspring);
#endif
					close (cnx);
				}
			}
		}
	}

	signal (SIGINT, oldsigint);

	/* Master process exits in error if no offspring was created */
	if (nr == 0) {
		exit (sox);
	}
}


/* Orderly exit from the current process.  Due to the use of fork() in the
 * forgoing, that means just doing an exit().  But it is a useful API call,
 * since alternative implementations of the same API may work with threads,
 * which would require some decent self-termination call.
 */
void insrv_exit (int exitcode) {
	exit (exitcode);
}

/*
 * $Log: libsrv.c,v $
 * Revision 1.4  2002/10/02 13:28:11  vanrein
 * Added include file <sys/time.h> that is needed on Linux.
 *
 * Revision 1.3  2002/09/14 20:29:24  vanrein
 * Adapted libsrv to detect + shoot zombie child processes.  HTTPproxy adapted.
 *
 * Revision 1.2  2002/09/13 11:07:34  vanrein
 * Added more include-files to support portability to other Unices than Linux.
 *
 * Revision 1.1  2002/09/12 05:44:44  vanrein
 * The first checkin of libsrv -- this compiles to a good .so
 * The hello demo works properly.
 * Due to resolver differences, this only compiles on Linux for now.
 *
 */
