/*
 * Copyright 1994 by OpenVision Technologies, Inc.
 * 
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appears in all copies and
 * that both that copyright notice and this permission notice appear in
 * supporting documentation, and that the name of OpenVision not be used
 * in advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission. OpenVision makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 * 
 * OPENVISION DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL OPENVISION BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF
 * USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <winsock.h>
#else
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif

#include <gssapi/gssapi_generic.h>
#include "gss-utils.h"

int verbose = 0;
int INETD = 0;
int READFD = 0;
int WRITEFD = 1;

void usage()
{
     fprintf(stderr, "Usage: gss-client [-port port] [-mech mechanism] [-d]\n");
     fprintf(stderr, "       [-f] [-q] [-ccount count] [-mcount count]\n");
     fprintf(stderr, "       [-na] [-nw] [-nx] [-nm] host service msg\n");
     exit(1);
}



static void parse_oid(char *mechanism, gss_OID *oid)
{
    char	*mechstr = 0, *cp;
    gss_buffer_desc tok;
    OM_uint32 maj_stat, min_stat;
    
    if (isdigit(mechanism[0])) {
	mechstr = malloc(strlen(mechanism)+5);
	if (!mechstr) {
	    fprintf(stderr, "Couldn't allocate mechanism scratch!\n");
	    return;
	}
	sprintf(mechstr, "{ %s }", mechanism);
	for (cp = mechstr; *cp; cp++)
	    if (*cp == '.')
		*cp = ' ';
	tok.value = mechstr;
    } else
	tok.value = mechanism;
    tok.length = strlen(tok.value);
    maj_stat = gss_str_to_oid(&min_stat, &tok, oid);
    if (maj_stat != GSS_S_COMPLETE) {
	display_status("while str_to_oid", maj_stat, min_stat);
	return;
    }
    if (mechstr)
	free(mechstr);
}




/*
 * Function: connect_to_server
 *
 * Purpose: Opens a TCP connection to the name host and port.
 *
 * Arguments:
 *
 * 	host		(r) the target host name
 * 	port		(r) the target port, in host byte order
 *
 * Returns: the established socket file desciptor, or -1 on failure
 *
 * Effects:
 *
 * The host name is resolved with gethostbyname(), and the socket is
 * opened and connected.  If an error occurs, an error message is
 * displayed and -1 is returned.
 */
int connect_to_server(host, port)
     char *host;
     u_short port;
{
     struct sockaddr_in saddr;
     struct hostent *hp;
     int s;
     
     if ((hp = gethostbyname(host)) == NULL) {
	  fprintf(stderr, "Unknown host: %s\n", host);
	  return -1;
     }
     
     saddr.sin_family = hp->h_addrtype;
     memcpy((char *)&saddr.sin_addr, hp->h_addr, sizeof(saddr.sin_addr));
     saddr.sin_port = htons(port);

     if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
	  perror("creating socket");
	  return -1;
     }
     if (connect(s, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
	  perror("connecting to server");
	  (void) close(s);
	  return -1;
     }
     return s;
}

/*
 * Function: client_establish_context
 *
 * Purpose: establishes a GSS-API context with a specified service and
 * returns the context handle
 *
 * Arguments:
 *
 * 	s		(r) an established TCP connection to the service
 * 	service_name	(r) the ASCII service name of the service
 *	deleg_flag	(r) GSS-API delegation flag (if any)
 *	auth_flag	(r) whether to actually do authentication
 *	oid		(r) OID of the mechanism to use
 * 	context		(w) the established GSS-API context
 *	ret_flags	(w) the returned flags from init_sec_context
 *
 * Returns: 0 on success, -1 on failure
 *
 * Effects:
 * 
 * service_name is imported as a GSS-API name and a GSS-API context is
 * established with the corresponding service; the service should be
 * listening on the TCP connection s.  The default GSS-API mechanism
 * is used, and mutual authentication and replay detection are
 * requested.
 * 
 * If successful, the context handle is returned in context.  If
 * unsuccessful, the GSS-API error messages are displayed on stderr
 * and -1 is returned.
 */
int client_establish_context(service_name, deleg_flag, 
			     gss_context, ret_flags)
     char *service_name;
     OM_uint32 deleg_flag;
     gss_ctx_id_t *gss_context;
     OM_uint32 *ret_flags;
{

     gss_OID oid = GSS_C_NULL_OID;
     gss_buffer_desc send_tok, recv_tok, *token_ptr;
     gss_name_t target_name;
     OM_uint32 maj_stat, min_stat, init_sec_min_stat;
     int token_flags;

     parse_oid("1.2.840.113554.1.2.2", &oid);

       /*
	* Import the name into target_name.  Use send_tok to save
	* local variable space.
	*/
       send_tok.value = service_name;
       send_tok.length = strlen(service_name) + 1;
       maj_stat = gss_import_name(&min_stat, &send_tok,
				  (gss_OID) gss_nt_user_name, &target_name);
       if (maj_stat != GSS_S_COMPLETE) {
	 display_status("while parsing name", maj_stat, min_stat);
	 return -1;
       }
     
       if (send_token(TOKEN_NOOP|TOKEN_CONTEXT_NEXT, empty_token) < 0) {
	 (void) gss_release_name(&min_stat, &target_name);
	 return -1;
       }

       /*
	* Perform the context-establishement loop.
	*
	* On each pass through the loop, token_ptr points to the token
	* to send to the server (or GSS_C_NO_BUFFER on the first pass).
	* Every generated token is stored in send_tok which is then
	* transmitted to the server; every received token is stored in
	* recv_tok, which token_ptr is then set to, to be processed by
	* the next call to gss_init_sec_context.
	* 
	* GSS-API guarantees that send_tok's length will be non-zero
	* if and only if the server is expecting another token from us,
	* and that gss_init_sec_context returns GSS_S_CONTINUE_NEEDED if
	* and only if the server has another token to send us.
	*/
     
       token_ptr = GSS_C_NO_BUFFER;
       *gss_context = GSS_C_NO_CONTEXT;

       do {
	 maj_stat =
	   gss_init_sec_context(&init_sec_min_stat,
				GSS_C_NO_CREDENTIAL,
				gss_context,
				target_name,
				oid,
				GSS_C_MUTUAL_FLAG | GSS_C_REPLAY_FLAG |
				deleg_flag,
				0,
				NULL,	/* no channel bindings */
				token_ptr,
				NULL,	/* ignore mech type */
				&send_tok,
				ret_flags,
				NULL);	/* ignore time_rec */

	 if (token_ptr != GSS_C_NO_BUFFER)
	   (void) gss_release_buffer(&min_stat, &recv_tok);

	 if (send_tok.length != 0) {
	   if (verbose)
	     printf("Sending init_sec_context token (size=%d)...",
		    send_tok.length);
	   if (send_token(TOKEN_CONTEXT, &send_tok) < 0) {
	     (void) gss_release_buffer(&min_stat, &send_tok);
	     (void) gss_release_name(&min_stat, &target_name);
	     return -1;
	   }
	 }
	 (void) gss_release_buffer(&min_stat, &send_tok);
 
	 if (maj_stat!=GSS_S_COMPLETE && maj_stat!=GSS_S_CONTINUE_NEEDED) {
	      display_status("while initializing context", maj_stat,
			     init_sec_min_stat);
	      (void) gss_release_name(&min_stat, &target_name);
	      if (*gss_context == GSS_C_NO_CONTEXT)
		      gss_delete_sec_context(&min_stat, gss_context,
					     GSS_C_NO_BUFFER);
	      return -1;
	 }
	  
	 if (maj_stat == GSS_S_CONTINUE_NEEDED) {
	   if (verbose)
	     printf("continue needed...");
	   if (recv_token(&token_flags, &recv_tok) < 0) {
	     (void) gss_release_name(&min_stat, &target_name);
	     return -1;
	   }
	   token_ptr = &recv_tok;
	 }
	 if (verbose)
	   printf("\n");
       } while (maj_stat == GSS_S_CONTINUE_NEEDED);

       (void) gss_release_name(&min_stat, &target_name);
       (void) gss_release_oid(&min_stat, &oid);

     return 0;
}




int process_response(context)
     gss_ctx_id_t context;
{

     char	*cp;
     int token_flags;
     int errorcode;
     int length;
     char* body;
     char* msg;
     int msglength;

     if (gss_recvmsg(context, &token_flags, &msg, &msglength) < 0)
       return(-1);

     cp = msg;

     if (token_flags & TOKEN_ERROR) {
       bcopy(cp, &errorcode, sizeof(int)); cp+=sizeof(int);
       printf("Error code: %d\n", errorcode);
     }

     bcopy(cp, &length, sizeof(int)); cp+=sizeof(int);

     if (length > MAXCMDLINE || length < 0) length = MAXCMDLINE;
     if (length > msglength) {
       fprintf(stderr, "Data unpacking error");
       return(-1);
     }

     body = malloc(length*sizeof(char)+1);
     bcopy(cp, body, length);
     body[length] = '\0';

     printf("Message: %s\n", body);

     return(0);

}




/*
 * Function: call_server
 *
 * Purpose: Call the "sign" service.
 *
 * Arguments:
 *
 * 	host		(r) the host providing the service
 * 	port		(r) the port to connect to on host
 * 	service_name	(r) the GSS-API service name to authenticate to
 *	deleg_flag	(r) GSS-API delegation flag (if any)
 *	auth_flag	(r) whether to do authentication
 *	wrap_flag	(r) whether to do message wrapping at all
 *	encrypt_flag	(r) whether to do encryption while wrapping
 *	mic_flag	(r) whether to request a MIC from the server
 * 	msg		(r) the message to have "signed"
 *	use_file	(r) whether to treat msg as an input file name
 *	mcount		(r) the number of times to send the message
 *
 * Returns: 0 on success, -1 on failure
 *
 * Effects:
 * 
 * call_server opens a TCP connection to <host:port> and establishes a
 * GSS-API context with service_name over the connection.  It then
 * seals msg in a GSS-API token with gss_wrap, sends it to the server,
 * reads back a GSS-API signature block for msg from the server, and
 * verifies it with gss_verify.  -1 is returned if any step fails,
 * otherwise 0 is returned.  */
int process_request(context, argc, argv)
     gss_ctx_id_t context;
     int argc;
     char** argv;
{
     int length = 0;
     char* msg;
     int msglength=0;
     char** cp;
     char* mp;
     int i;

     cp = argv;
     for (i=0; i<argc; i++) {
       msglength += strlen(*cp++);
     }
     msglength += argc * sizeof(int);

     msg = malloc(msglength * (sizeof(char)));

     mp = msg;
     cp = argv;
     while (argc != 0) {
       length = strlen(*cp);
       bcopy(&length, mp, sizeof(int)); mp+=sizeof(int);
       bcopy(*cp, mp, length); mp+=length;
       argc--;
       cp++;
     }

     if (gss_sendmsg(context, TOKEN_DATA, msg, msglength) < 0)
       return(-1);

     return(0);

}

int main(argc, argv)
     int argc;
     char **argv;
{
     char *service_name, *server_host;
     u_short port = 4444;
     OM_uint32 deleg_flag = 0, ret_flags, maj_stat, min_stat;
     int s;
     gss_ctx_id_t context;
     gss_buffer_desc out_buf;

     display_file = stdout;

     /* Parse arguments. */
     argc--; argv++;
     while (argc) {
	  if (strcmp(*argv, "-port") == 0) {
	       argc--; argv++;
	       if (!argc) usage();
	       port = atoi(*argv);
	   } else if (strcmp(*argv, "-v") == 0) {
	       verbose = 1;
	   } else if (strcmp(*argv, "-d") == 0) {
	       deleg_flag = GSS_C_DELEG_FLAG;
	  } else if (strcmp(*argv, "-q") == 0) {
	       verbose = 0;
	  } else 
	       break;
	  argc--; argv++;
     }
     if (argc < 3)
	  usage();

     server_host = *argv++;
     argc--;
     service_name = *argv++;
     argc--;

     /* Open connection */
     if ((s = connect_to_server(server_host, port)) < 0)
	  return -1;

     INETD = 0;
     READFD = s;
     WRITEFD = s;

     /* Establish context */
     if (client_establish_context(service_name, deleg_flag, &context, 
				  &ret_flags) < 0) {
	  (void) close(s);
	  return -1;
     }

     /* display the flags */
     display_ctx_flags(ret_flags);

     if (process_request(context, argc, argv) < 0)
       exit(1);

     if (process_response(context) < 0)
       exit(1);

     /* Delete context */
     maj_stat = gss_delete_sec_context(&min_stat, &context, &out_buf);
     if (maj_stat != GSS_S_COMPLETE) {
       display_status("while deleting context", maj_stat, min_stat);
       (void) close(s);
       (void) gss_delete_sec_context(&min_stat, &context, GSS_C_NO_BUFFER);
       return -1;
     }
     
     (void) gss_release_buffer(&min_stat, &out_buf);
     (void) close(s);
	 
     return 0;
}
