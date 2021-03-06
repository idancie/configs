/*
*
*  DHCPv6 load generator
*  
*
*/

#include "dras6.h"
int main(int argc, char **argv)
{
	struct sockaddr_in6 ca;
	int ret;
	dhcp_server_t *server;

	if (getuid()){
		fprintf(stderr,"\n\tThis program must be run as root\n");
		exit(1);
	}
	logfp = stderr;
	time(&start_time);
	parse_args(argc, argv);


	srand(getpid() + time(NULL));
        if ( logfile == NULL )
                logfp = stderr;
	else {
        	logfp = fopen(logfile, "w");
		if ( logfp == NULL){
			fprintf(stderr,"Open %s failed\n", logfile);
			exit(1);
		}
		setvbuf(logfp, NULL, _IONBF, 0);
	}
        fprintf(logfp,"Begin: Version %s\n", version);

	if (output_file != NULL){
		outfp = fopen(output_file,"w");
		if (outfp == NULL){
			fprintf(logfp,"Open failed: %s\n",output_file);
			exit(1);
		}
	}
	if (!memcmp(&srcaddr,&in6addr_any, sizeof(struct in6_addr)))
	    srcaddr = get_local_addr();
	signal(SIGPIPE, SIG_IGN);

	sock=socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0){
		perror("socket:");
		exit(1);
	}

	ret = setsockopt(sock, SOL_SOCKET, SO_RCVBUF,
			(char *) &socket_bufsize, sizeof(socket_bufsize));
	if (ret < 0)
		fprintf(stderr, "Warning:  setsockbuf(SO_RCVBUF) failed\n");

        ret = setsockopt(sock, SOL_SOCKET, SO_SNDBUF,
			(char *) &socket_bufsize, sizeof(socket_bufsize));
        if (ret < 0)
		fprintf(stderr, "Warning:  setsockbuf(SO_SNDBUF) failed\n");

	memset(&ca, 0, sizeof(struct sockaddr_in6));
	ca.sin6_family = AF_INET6;
	ca.sin6_port = htons(DHCP6_LOCAL_PORT);

	memcpy(&ca.sin6_addr, &srcaddr, sizeof(struct in6_addr));
	if (bind(sock, (struct sockaddr *)&ca, sizeof(ca))< 0 ){
		perror("bind");
		exit(1);
	}

	for (server=servers; server != NULL; server=server->next){
		server->list = malloc(max_sessions * sizeof(dhcp_session_t));
		memset(server->list, '\0', max_sessions * sizeof(dhcp_session_t));
	}

	sender();
	return(test_statistics());
}

/*
    Test complete when sessions_started = completed + timeouts
*/
void sender(void)
{
	dhcp_session_t		*session;
	dhcp_server_t		*current_server = servers;
	dhcp_server_t		*s;
	lease_data_t		*leases = NULL;
	lease_data_t		*lease;
	uint32_t		ntransactions = 0;
	int			complete = 0;

	if ( input_file != NULL &&
			(number_requests=read_lease_data(&leases)) == 0 ){
			exit(1);
	}
	lease = leases;
	while  (!complete){
		session = find_free_session(current_server);
		
		if (session == NULL){
                        if (num_servers > 1)
                                getmac(NULL);
		}
		else {
			if (send_delay)
				poll(NULL, 0, send_delay);

			if (input_file != NULL){
				if (lease != NULL){
					ntransactions++;
					if (memcmp(&lease->sa, &in6addr_any, sizeof(struct in6_addr))){
 					    for (s = servers; s != NULL; s = s->next)
                                    		if (memcmp(&s->sa.sin6_addr, &lease->sa, sizeof(struct in6_addr))){
                                           		current_server = s;
                                           		break;
                                    		}
					}
					fill_session(session, lease);
					send_packet6(start_from, session, current_server);
					lease=lease->next;
				}
			}
			else if (ntransactions < number_requests){
				ntransactions++;
				fill_session(session, NULL);
				send_packet6(DHCPV6_SOLICIT, session, current_server);
			}
		}
		reader();
		complete = process_sessions();
		//fprintf(stderr,"Complete: %u ntrans %u\n", complete, ntransactions);
		if ( current_server->next == NULL)
			current_server = servers;
		else
			current_server = current_server->next;
	}
}
void fill_session(dhcp_session_t *session, lease_data_t *lease)
{
	int i;

	if (lease != NULL){
		session->transaction_id = session->mac + 3;
		session->num_ia = lease->num_ia;
		for (i=0; i< lease->num_ia; i++){
			session->ia[i].ipaddr = lease->ia[i].ipaddr;
			session->ia[i].prefix_len = lease->ia[i].prefix_len;
			session->ia[i].iaid = lease->ia[i].iaid;
		}
		memcpy(session->serverid, lease->serverid, lease->serverid_len);
		session->serverid_len = lease->serverid_len;
		memcpy(session->mac,lease->mac,6);
		if (lease->hostname != NULL)
			strncpy(session->hostname, lease->hostname, 
				sizeof(session->hostname));
		else
			*(session->hostname) = '\0';

	}
	else {
		getmac(session->mac);
		session->num_ia = num_per_mac;
		for (i=0; i < num_per_mac; i++){
			session->ia[i].iaid = ++ia_id * 1000 + i;
			session->ia[i].ipaddr = in6addr_any;
			session->ia[i].prefix_len = 0;
		}
		if (send_hostname) {
			if (random_hostname)
				snprintf(session->hostname, sizeof(session->hostname),
					"h%u%u.%s", rand(), rand(),
					update_domain ? update_domain : "");
			else
				snprintf(session->hostname,sizeof(session->hostname),
					"h%02x%02x%02x%02x%02x%02x.%s",
					session->mac[0], session->mac[1],
					session->mac[2], session->mac[3], session->mac[4],
				session->mac[5], update_domain ? update_domain : "");
		}
		else {
			*(session->hostname) = '\0';
		}
		session->serverid[0] = 0xff;
		session->serverid_len = 0;
	}
	return;
}
int test_statistics(void)
{
	dhcp_server_t	*iter;
	double	elapsed;
	int	retval = 0;
	char	ipstr[INET6_ADDRSTRLEN];

	fprintf(logfp, "\nTest started:         %s\n",
			ctime(&start_time));
	for (iter=servers; iter != NULL; iter=iter->next){
		inet_ntop(AF_INET6, &iter->sa.sin6_addr, ipstr, sizeof(ipstr));
		fprintf(logfp,"Server %-s\n", ipstr);
		if ( iter->stats.solicit_acks_received +
			iter->stats.solicit_naks_received +
			iter->stats.request_acks_received +
			iter->stats.request_naks_received +
			iter->stats.renew_acks_received +
			iter->stats.renew_naks_received +
			iter->stats.rebind_acks_received +
			iter->stats.rebind_naks_received +
			iter->stats.inform_acks_received +
			iter->stats.inform_naks_received +
			iter->stats.confirm_acks_received +
			iter->stats.confirm_naks_received +
			iter->stats.release_acks_received +
			iter->stats.release_naks_received == 0){
			fprintf(logfp,"\n\tNo replies received.\n");
			retval = 1;
			continue;
		}

                if (iter->stats.failed + iter->stats.errors > 0 )
			retval = 1;
		fprintf(logfp,"Solicits sent:          %6u\n",
				iter->stats.solicits_sent);
		fprintf(logfp,"Solicit Acks Received:  %6u\n",
				iter->stats.solicit_acks_received);
		fprintf(logfp,"Solicit Naks Received:  %6u\n",
				iter->stats.solicit_naks_received);

		fprintf(logfp,"Requests sent:          %6u\n",
					iter->stats.requests_sent);
		fprintf(logfp,"Request Acks Received:  %6u\n",
					iter->stats.request_acks_received);
		fprintf(logfp,"Request Naks received:  %6u\n",
					iter->stats.request_naks_received);
		if (iter->stats.renews_sent){
			fprintf(logfp,"Renew Acks Received:    %6u\n",
					iter->stats.renew_acks_received);
			fprintf(logfp,"Renew Naks received:    %6u\n",
					iter->stats.renew_naks_received);
		}

		if (iter->stats.releases_sent){
			fprintf(logfp,"Releases sent:          %6u\n",iter->stats.releases_sent);
			fprintf(logfp,"Release Acks Received:  %6u\n",
				iter->stats.release_acks_received);
			fprintf(logfp,"Release Naks Received:  %6u\n",
				iter->stats.release_naks_received);
		}
		if (iter->stats.declines_sent){
			fprintf(logfp,"Declines sent:          %6u\n",iter->stats.declines_sent);
			fprintf(logfp,"Decline Acks Received:  %6u\n",
				iter->stats.decline_acks_received);
			fprintf(logfp,"Decline Naks Received:  %6u\n",
				iter->stats.decline_naks_received);
		}
		if (iter->stats.informs_sent){
			fprintf(logfp,"Information Requests sent:      %6u\n",
					iter->stats.informs_sent);
			fprintf(logfp,"Information Request Acks Received: %5u\n",
				iter->stats.inform_acks_received);
			fprintf(logfp,"Information Request Naks Received: %5u\n",
				iter->stats.inform_naks_received);
		}
		if (iter->stats.rebinds_sent){
			fprintf(logfp,"Rebinds sent:                   %6u\n",
					iter->stats.rebinds_sent);
			fprintf(logfp,"Rebind Acks Received:  %6u\n",
				iter->stats.rebind_acks_received);
		}
		if (iter->stats.confirms_sent){
			fprintf(logfp,"Confirms sent:                  %6u\n",
					iter->stats.confirms_sent);
			fprintf(logfp,"Confirm Acks Received:            %6u\n",
				iter->stats.confirm_acks_received);
		}

		fprintf(logfp,"Advertise timeouts:     %6u\n",
			iter->stats.solicit_ack_timeouts);
		fprintf(logfp,"Request Timeouts:       %6u\n",
			iter->stats.request_ack_timeouts);
		fprintf(logfp,"Renew Timeouts:         %6u\n",
			iter->stats.renew_ack_timeouts);
		elapsed = DELTATV(iter->last_packet_received,
					iter->first_packet_sent)/1000000.000;

		fprintf(logfp,"Completed:              %6u\n",iter->stats.completed);
		fprintf(logfp,"Failed:                 %6u\n",iter->stats.failed);
		fprintf(logfp,"Errors:                 %6u\n",iter->stats.errors);
		fprintf(logfp,"Elapsed time:         %15.2f secs\n", elapsed);
		fprintf(logfp,"Advertise Latency (Min/Max/Avg): %.3f/%.3f/%.3f (ms)\n",
		0.0010 * (double) iter->stats.advertise_latency_min,
		0.0010 * (double)iter->stats.advertise_latency_max,
		0.0010 * (double) iter->stats.advertise_latency_avg / (double)
		(iter->stats.solicit_acks_received + iter->stats.solicit_naks_received ?
		 	iter->stats.solicit_acks_received + iter->stats.solicit_naks_received : 1));
		fprintf(logfp,"Reply Latency (Min/Max/Avg):     %.3f/%.3f/%.3f (ms)\n",
		0.0010 * (double) iter->stats.reply_latency_min,
		0.0010 * (double) iter->stats.reply_latency_max,
		0.0010 * (double) iter->stats.reply_latency_avg / (double)
		(iter->stats.request_acks_received ? iter->stats.request_acks_received : 1));
		fprintf(logfp,"Advertise Acks/sec:            %6.2f\n",
			(double)iter->stats.solicit_acks_received/elapsed);
		fprintf(logfp,"Leases/sec:                    %6.2f\n",
			(double)iter->stats.completed/elapsed);

		fprintf(logfp, "-----------------------------------------\n");
	}
	fprintf(logfp,"Return value: %d\n", retval);
	return(retval);
}

int send_packet6(uint8_t type, dhcp_session_t *session, dhcp_server_t *server)
{
	uint8_t			buffer[1024]; /* Fix to DHCP_MTU */
	struct dhcpv6_packet	*packet;
	struct timeval		timestamp;
	int			offset=0, i=0, j=0;
	int			dhcp_msg_len;

	//fprintf(logfp,"Entering send_packet6");

	packet = (struct dhcpv6_packet *) buffer;
	if (use_relay){  // Add relay header 
		buffer[0] = DHCPV6_RELAY_FORW; //msg type
		buffer[1] = 1; // Relay MSG Hops
		memcpy(buffer + 2, &srcaddr, sizeof(struct in6_addr));
		memcpy(buffer + 18, &session->ia[0].ipaddr, sizeof(struct in6_addr));
		*((uint16_t *) (buffer + 34)) = htons(D6O_RELAY_MSG);
		packet = (struct dhcpv6_packet *) ( buffer + 38);
	}

		
	packet->msg_type = type;
	for (j = 0; j < 3; j++)
		packet->transaction_id[j] = session->mac[j+3];

	if (opt_seq){
		offset += add_opt_seq(type, packet->options, session);
	}
	else {
		/* CLIENT_ID option */
		*((uint16_t *) (packet->options + offset)) = htons(D6O_CLIENTID);
		offset += 2;
		*((uint16_t *) (packet->options + offset)) = htons(DUID_LLT_LEN);
		offset += 2;
		*((uint16_t *) (packet->options + offset)) = htons(1);
		offset += 2;
		*((uint16_t *) (packet->options + offset)) = htons(1);
		offset += 2;
		memset(packet->options + offset, 0x88, 4);
		offset += 4;
		memcpy(packet->options + offset, session->mac, 6);
		offset += 6;
	
		/* request Status Code from server */
		*((uint16_t *) (packet->options + offset)) = htons(D6O_STATUS_CODE);
		offset += 2;
		*((uint16_t *) (packet->options + offset)) = htons(2);
		offset += 2;
		*((uint16_t *) (packet->options + offset)) = htons(STATUS_Success);
		offset += 2;
	
		if (type != DHCPV6_INFORMATION_REQUEST)
			offset += fill_iafu_mess(session, packet->options + offset);
	
        	if (type == DHCPV6_SOLICIT && rapid_commit){
                	*((uint16_t *) (packet->options + offset)) = htons(D6O_RAPID_COMMIT);
                	offset += 2;
                	*((uint16_t *) (packet->options + offset)) = 0;
                	offset += 2;
        	}
	
		if (session->serverid[0] != 0xff){
			*((uint16_t *) (packet->options + offset)) = htons(D6O_SERVERID);
			offset += 2;
			*((uint16_t *) (packet->options + offset)) =
						htons(session->serverid_len);
			offset += 2;
			memcpy(packet->options + offset, session->serverid,
				session->serverid_len);
			offset += session->serverid_len;
		}
		if (session->hostname[0] != '\0')
			offset += pack_client_fqdn(packet->options + offset, session->hostname);

		/*  Ask for all -I options */
		if (nrequests){
			*((uint16_t *) (packet->options + offset)) = htons(D6O_ORO);
			offset += 2;
			*((uint16_t *) (packet->options + offset)) = htons(nrequests * sizeof(uint16_t));
			offset += 2;
			memcpy(packet->options + offset, info_requests, nrequests * sizeof(uint16_t));
			offset += nrequests * sizeof(uint16_t);
		}

		/* append any user defined options */
		if (nextraoptions) {
			for (i = 0; i < nextraoptions; i++) {
				*((uint16_t *) (packet->options + offset)) = htons(extraoptions[i]->option_no);
				offset += 2;
				*((uint16_t *) (packet->options + offset)) = htons(extraoptions[i]->data_len);
				offset += 2;
				memcpy(packet->options + offset, extraoptions[i]->data, extraoptions[i]->data_len);
				offset += extraoptions[i]->data_len;
			}
		}
	}

	gettimeofday(&timestamp, NULL);
	if (session->session_start == 0)
		session->session_start = timestamp.tv_sec;

	/* Set the relayed message option length for relay agents */
	dhcp_msg_len = offset + 4 ;
	// printf("\nDHCP packet size: %u\n", dhcp_msg_len);
	if (use_relay){
		*((uint16_t *) (buffer + 36)) =  htons(dhcp_msg_len);
		dhcp_msg_len += 38;
	}
	/* send the packet */
	if (sendto(sock, buffer, dhcp_msg_len, 0,
	   (struct sockaddr *)&server->sa, sizeof(struct sockaddr_in6)) < 0 ){
		fprintf(logfp,"sendto failed:\n");
		return(-1);
	}

	if (verbose)
		print_packet(use_relay ? dhcp_msg_len - 38 : dhcp_msg_len, packet, "Sent: ");

	/* Update statistics */
	server->last_packet_sent.tv_sec = timestamp.tv_sec;
	server->last_packet_sent.tv_usec = timestamp.tv_usec;
	session->last_sent.tv_sec = timestamp.tv_sec;
	session->last_sent.tv_usec = timestamp.tv_usec;
	session->type_last_sent = type;
	if (server->first_packet_sent.tv_sec == 0){
		server->first_packet_sent.tv_sec = timestamp.tv_sec;
		server->first_packet_sent.tv_usec = timestamp.tv_usec;
	}

	if (type == DHCPV6_SOLICIT) {
		server->stats.solicits_sent++;
		if (rapid_commit)
			session->state |= RAPID_SOLICIT_SENT;
		else
			session->state |= SOLICIT_SENT;

	}
	else if (type == DHCPV6_REQUEST){
		server->stats.requests_sent++;
		session->state |= REQUEST_SENT;
	}
	else if (type == DHCPV6_RENEW){
		server->stats.renews_sent++;
		session->state |= RENEW_SENT;
	}
	else if (type == DHCPV6_REBIND){
		server->stats.rebinds_sent++;
		session->state |= REBIND_SENT;
	}
	else if (type == DHCPV6_INFORMATION_REQUEST){
		server->stats.informs_sent++;
		session->state |= INFORM_SENT;
	}
	else if (type == DHCPV6_RELEASE){
		server->stats.releases_sent++;
		session->state |= RELEASE_SENT;
	}
	else if (type == DHCPV6_DECLINE){
		server->stats.declines_sent++;
		session->state |= DECLINE_SENT;
	}
	else if (type == DHCPV6_CONFIRM){
		server->stats.confirms_sent++;
		session->state |= CONFIRM_SENT;
	}
	else {
		fprintf(logfp, "send_packet6: Unknown packet %u", type);
	}


	//fprintf(logfp,"Leaving send_packet6");
	return(0);
}
int fill_iafu_mess(dhcp_session_t *session, uint8_t *option)
{
	uint32_t	offset =0;
	int		idx, send_ia;
	uint8_t		*option_start;

	/* 
	 * We have the ugly prospect of requesting multiple prefixes for
	 * IA_PD or multiple addresses for IA_NA.

	 * The number of IA options to send is either num_ia, is this if our first packet
	 * or recv_ia which is the number of IA options received from server.
	*/
	send_ia = (session->recv_ia > 0) ? session->recv_ia :  session->num_ia;
	for (idx=0; idx < send_ia; idx++){
	   option_start = option + offset;
	   if (request_prefix || session->ia[idx].prefix_len > 0) {
		/* IA_PD */
		*((uint16_t *) (option + offset)) = htons(D6O_IA_PD);
		offset += 4;
		*((uint32_t *) (option + offset)) =
				htonl(session->ia[idx].iaid);
		offset += 4;
		memset(option + offset, 0, 8); /* T1 = 0, T2 = 0 */
		offset += 8;

		/* IA_PD_options: IAPREFIX */
		*((uint16_t *) (option + offset)) = htons(D6O_IAPREFIX);
		offset += 2;

		/* 16 bytes: IPv6 prefix + 8 bytes: lease times + 1 byte: prefix-length */
		*((uint16_t *) (option + offset)) = htons(25);
		offset += 2;

		/* we accept any lease times */
		memset(option + offset, 0, 8);
		offset += 8;

		/* prefix-length */
		*(option + offset) = session->ia[idx].prefix_len;
		offset += 1;

		/* IP prefix */
		memcpy(option + offset, &session->ia[idx].ipaddr, sizeof(struct in6_addr));
		offset += sizeof(struct in6_addr);
	}
	else {
		/* IA_NA */
		*((uint16_t *) (option + offset)) = htons(D6O_IA_NA);
		offset += 4;
		*((uint32_t *) (option + offset)) = 
				htonl(session->ia[idx].iaid);
		offset += 4;

		memset(option + offset, 0, 8); /* T1 = 0, T2 = 0 */
		offset += 8;

		/* IA_NA_options: IAADDR */
		*((uint16_t *) (option + offset)) = htons(D6O_IAADDR);
		offset += 2;

		/* IADDR option-len: 16 bytes: IPv6 + 8 bytes: lease times */
		*((uint16_t *) (option + offset)) = htons(24);
		offset += 2;

		/* IP address */
		memcpy(option + offset, &session->ia[idx].ipaddr, sizeof(struct in6_addr));
		offset += sizeof(struct in6_addr);

		/* we accept any lease times */
		memset(option + offset, 0x80, 8);
		offset += 8;
	   }
	   *((uint16_t *) (option_start + 2)) = 
			htons(((option + offset) - option_start) - 4 );
	}
	return(offset);

}
int  process_sessions(void)
{
	dhcp_session_t *session;
	dhcp_server_t *server;
	uint32_t sent=0, completed=0, failed=0;
	struct timeval	now;
	int		i,j;

	gettimeofday(&now, NULL);
	for (server=servers; server != NULL; server=server->next){
	for (i = 0,j=0; j< server->active && i < max_sessions; i++){
		session = server->list + i;
		if (session->state == UNALLOCATED)
			continue;
		j++;
		switch (session->state){
			case SESSION_ALLOCATED:
			break;
			case PACKET_ERROR:
				server->stats.errors++;
				server->stats.failed++;
				memset(session, '\0', sizeof(dhcp_session_t));
				server->active--;
			break;
			case SESSION_ALLOCATED|SOLICIT_SENT:
			case SESSION_ALLOCATED|RAPID_SOLICIT_SENT:
				if ( DELTATV(now, session->last_sent) > timeout ){
					server->stats.solicit_ack_timeouts++;
					if (retransmit > session->timeouts || send_until_answered){
						session->timeouts++;
						send_packet6(DHCPV6_SOLICIT, session, server);
					}
					else {
						server->stats.failed++;
						memset(session, '\0', sizeof(dhcp_session_t));
						server->active--;
					}
				}
			break;
			case SESSION_ALLOCATED|SOLICIT_SENT|SOLICIT_ACK:
				if (dhcp_ping == 1){
					server->stats.completed++;
					memset(session, '\0', sizeof(dhcp_session_t));
					server->active--;

					continue;
				}
				session->timeouts = 0;
				send_packet6(DHCPV6_REQUEST, session, server);
			break;
			case SESSION_ALLOCATED|SOLICIT_SENT|SOLICIT_NAK:
			case SESSION_ALLOCATED|RAPID_SOLICIT_SENT|SOLICIT_NAK:
				server->stats.errors++;
				server->stats.failed++;
				memset(session, '\0', sizeof(dhcp_session_t));
				server->active--;
			break;
			case SESSION_ALLOCATED|SOLICIT_SENT|SOLICIT_ACK|REQUEST_SENT:
			case SESSION_ALLOCATED|REQUEST_SENT:
				if ( DELTATV(now, session->last_sent) > timeout ){
					server->stats.request_ack_timeouts++;
					if (retransmit > session->timeouts || send_until_answered){
						session->timeouts++;
						send_packet6(DHCPV6_REQUEST, session, server);
					}
					else {
						session->timeouts++;
						server->stats.failed++;
						memset(session, '\0', sizeof(dhcp_session_t));
						server->active--;
					}
				}
			break;
			case SESSION_ALLOCATED|SOLICIT_SENT|SOLICIT_ACK|REQUEST_SENT|REQUEST_ACK:
			case SESSION_ALLOCATED|RAPID_SOLICIT_SENT|SOLICIT_ACK:
			case SESSION_ALLOCATED|REQUEST_SENT|REQUEST_ACK:
			case SESSION_ALLOCATED|REQUEST_SENT|REQUEST_NAK:
				if (send_release){
					send_packet6(DHCPV6_RELEASE, session, server);
				}
				else if (send_decline){
					send_packet6(DHCPV6_DECLINE, session, server);
				}
				else {
					server->stats.completed++;
					if (outfp)
						print_lease(outfp, session, &server->sa.sin6_addr);
					memset(session, '\0', sizeof(dhcp_session_t));
					server->active--;
				}
			break;
			case SESSION_ALLOCATED|SOLICIT_SENT|SOLICIT_ACK|REQUEST_SENT|REQUEST_NAK:
				server->stats.failed++;
				memset(session, '\0', sizeof(dhcp_session_t));
				server->active--;
			break;
			case SESSION_ALLOCATED|SOLICIT_SENT|SOLICIT_ACK|REQUEST_SENT|REQUEST_ACK|DECLINE_SENT:
				if ( DELTATV(now, session->last_sent) > timeout ){
					server->stats.decline_ack_timeouts++;
					if (retransmit > session->timeouts || send_until_answered){
						session->timeouts++;
						send_packet6(DHCPV6_DECLINE, session, server);
					}
					else {
						server->stats.failed++;
						memset(session, '\0', sizeof(dhcp_session_t));
						server->active--;
					}
				}
			break;
			case SESSION_ALLOCATED|SOLICIT_SENT|SOLICIT_ACK|REQUEST_SENT|REQUEST_ACK|RELEASE_SENT:
			case SESSION_ALLOCATED|RAPID_SOLICIT_SENT|SOLICIT_ACK|RELEASE_SENT:
			case SESSION_ALLOCATED|RELEASE_SENT:
				if ( DELTATV(now, session->last_sent) > timeout ){
					server->stats.release_ack_timeouts++;
					if (retransmit > session->timeouts || send_until_answered){
						session->timeouts++;
						send_packet6(DHCPV6_RELEASE, session, server);
					}
					else {
						server->stats.failed++;
						memset(session, '\0', sizeof(dhcp_session_t));
						server->active--;
					}
				}
			break;
			case SESSION_ALLOCATED|SOLICIT_SENT|SOLICIT_ACK|REQUEST_SENT|REQUEST_ACK|DECLINE_SENT|DECLINE_ACK:
			case SESSION_ALLOCATED|SOLICIT_SENT|SOLICIT_ACK|REQUEST_SENT|REQUEST_ACK|RELEASE_SENT|RELEASE_ACK:
			case SESSION_ALLOCATED|RAPID_SOLICIT_SENT|SOLICIT_ACK|RELEASE_SENT|RELEASE_ACK:
			case SESSION_ALLOCATED|RELEASE_SENT|RELEASE_ACK:
				server->stats.completed++;
				memset(session, '\0', sizeof(dhcp_session_t));
				server->active--;
			break;
			case SESSION_ALLOCATED|SOLICIT_SENT|SOLICIT_ACK|REQUEST_SENT|REQUEST_ACK|RELEASE_SENT|RELEASE_NAK:
			case SESSION_ALLOCATED|RAPID_SOLICIT_SENT|SOLICIT_ACK|RELEASE_SENT|RELEASE_NAK:
			case SESSION_ALLOCATED|RELEASE_SENT|RELEASE_NAK:
				server->stats.failed++;
				memset(session, '\0', sizeof(dhcp_session_t));
				server->active--;
			break;
			case SESSION_ALLOCATED|RENEW_SENT:
				if ( DELTATV(now, session->last_sent) > timeout ){
					server->stats.renew_ack_timeouts++;
					if (retransmit > session->timeouts || send_until_answered){
						session->timeouts++;
						send_packet6(DHCPV6_RENEW, session, server);
					}
					else {
						server->stats.failed++;
						memset(session, '\0', sizeof(dhcp_session_t));
						server->active--;
					}
				}

			break;
			case SESSION_ALLOCATED|RENEW_SENT|RENEW_ACK:
			case SESSION_ALLOCATED|RENEW_SENT|RENEW_NAK:
				if (session->state & RENEW_ACK)
					server->stats.completed++;
				else
					server->stats.failed++;
				memset(session, '\0', sizeof(dhcp_session_t));
				server->active--;
			break;
			case SESSION_ALLOCATED|INFORM_SENT:
				if ( DELTATV(now, session->last_sent) > timeout ){
					server->stats.inform_ack_timeouts++;
					if (retransmit > session->timeouts || send_until_answered){
						session->timeouts++;
						send_packet6(DHCPV6_INFORMATION_REQUEST, session, server);
					}
					else {
						server->stats.failed++;
						memset(session, '\0', sizeof(dhcp_session_t));
						server->active--;
					}
				}
			break;
			case SESSION_ALLOCATED|INFORM_SENT|INFORM_ACK:
			case SESSION_ALLOCATED|INFORM_SENT|INFORM_NAK:
				if (session->state & INFORM_ACK)
					server->stats.completed++;
				else
					server->stats.failed++;
				memset(session, '\0', sizeof(dhcp_session_t));
				server->active--;
			break;
			case SESSION_ALLOCATED|CONFIRM_SENT:
				if ( DELTATV(now, session->last_sent) > timeout ){
					server->stats.confirm_ack_timeouts++;
					if (retransmit > session->timeouts || send_until_answered){
						session->timeouts++;
						send_packet6(DHCPV6_CONFIRM, session, server);
					}
					else {
						server->stats.failed++;
						memset(session, '\0', sizeof(dhcp_session_t));
						server->active--;
					}
				}
			break;
			case SESSION_ALLOCATED|CONFIRM_SENT|CONFIRM_ACK:
			case SESSION_ALLOCATED|CONFIRM_SENT|CONFIRM_NAK:
				if (session->state & CONFIRM_ACK)
					server->stats.completed++;
				else
					server->stats.failed++;
				memset(session, '\0', sizeof(dhcp_session_t));
				server->active--;
			break;
			default:
				server->stats.errors++;
				server->stats.failed++;
				fprintf(logfp,"PS: Undefined session state %u\n", session->state);
				decode_state(session->state);
				fprintf(logfp,"\tLast Sent: %u.%u Type: %u\n"
					"\tLast Received: %u.%u Type: %u\n",
					(uint32_t)session->last_sent.tv_sec,
					(uint32_t)session->last_sent.tv_usec,
					(uint32_t)session->type_last_sent,
					(uint32_t)session->last_received.tv_sec,
					(uint32_t)session->last_received.tv_usec,
					(uint32_t)session->type_last_received);
					memset(session, '\0', sizeof(dhcp_session_t));
					server->active--;
		}

	}
		if ( input_file != NULL){
			sent += server->stats.requests_sent +
				 server->stats.renews_sent +
				 server->stats.declines_sent +
				 server->stats.informs_sent +
				 server->stats.rebinds_sent +
				 server->stats.confirms_sent;
			if (start_from == DHCPV6_RELEASE)
				sent += server->stats.releases_sent;
		}
		else
			sent += server->stats.solicits_sent;

		completed += server->stats.completed;
		failed    += server->stats.failed;
	}
	//fprintf(stderr, "Sent %u Completed %u Failed %u Requests: %u\n",
		//sent, completed, failed, number_requests);
	if ( sent >= number_requests){
		if (send_until_answered && completed < number_requests){
			return(0);
		}
		else if ( completed + failed >= number_requests){/* We are done */
			return(1);
		}
	}
	return(0);
}

int process_packet(void *p, struct timeval *timestamp, uint32_t length)
{
	int		found=0;
	dhcp_server_t	*server;
	dhcp_session_t	*session=NULL;
	dhcp_stats_t	*stats=NULL;
	struct dhcpv6_packet *packet = (struct dhcpv6_packet *) p;
	uint8_t 	*options = packet->options;
	uint32_t	offset = 0;
	uint32_t	dt;
	uint16_t	otype, olen;
	int		is_ack=1;
	int		i;

	// XXX Bad assumption that relay message is first option
	if (use_relay == 1 && *((char *)p) == DHCPV6_RELAY_REPL){
		length -= 38; //XXX  Assumes RELAY_REPLY is the only option!
		packet = (struct dhcpv6_packet *) ((char *)p + 38);// Skip relay hdr
		options = packet->options;
	}

	for (server=servers; server != NULL; server=server->next){
		for (i=0; i < max_sessions; i++){
			session = server->list + i;
			if (!memcmp(session->mac + 3, packet->transaction_id, 3)){
				found=1;
				break;
			}
		}
		if (found == 1)
			break;
	}
	// XXX  Set session->recv_ia to zero for now.  Hopefully these are returned in every reply
	session->recv_ia = 0;

	while ( offset < length - 4){
		otype =  ntohs(*((uint16_t *)(options + offset)));
		olen =  ntohs(*((uint16_t *)(options + offset + 2)));
		if (otype == 0){
			fprintf(logfp,"Error: Option is zero!\n");
			break;
		}
		//printf("process_packet: Option %u found\n", otype);
		if (otype == D6O_SERVERID){
			session->serverid_len =
				(olen < MAX_DUID_LEN) ? olen : MAX_DUID_LEN;
			memcpy(session->serverid, options + offset +4,
					session->serverid_len);
		}
		else if (otype == D6O_IA_NA || otype == D6O_IA_TA || otype == D6O_IA_PD){
			unsigned char *suboptions = options + offset + 16;
			int ia_offset = 0;
			while (ia_offset + 12 < olen){
				uint8_t sub_otype =  ntohs(*((uint16_t *)(suboptions + ia_offset)));
				uint8_t sub_olen =   ntohs(*((uint16_t *)(suboptions + ia_offset + 2)));
				if ( sub_otype == D6O_IAADDR){
					memcpy(&session->ia[session->recv_ia].ipaddr, 
						suboptions + ia_offset + 4, sizeof(struct in6_addr));
					session->ia[session->recv_ia].prefix_len = 0;
				}
				else if (sub_otype == D6O_IAPREFIX){
					session->ia[session->recv_ia].prefix_len =
								*(suboptions + ia_offset + 12);
					memcpy(&session->ia[session->recv_ia].ipaddr,
						suboptions + ia_offset + 13, sizeof(struct in6_addr));
				}
				else if (sub_otype == D6O_STATUS_CODE){
                        		if (ntohs(*((uint16_t *) (suboptions + ia_offset + 4))) != STATUS_Success)
                                		is_ack=0;
				}
				ia_offset += (sub_olen + 4);
			}
			session->recv_ia++;
		}
		else if (otype == D6O_STATUS_CODE){
                        if (ntohs(*((uint16_t *) (options + offset + 4))) != STATUS_Success)
                                is_ack=0;
			//fprintf(logfp,"Status Option: %u", ntohs(*((uint16_t *) (options + offset + 4))));
		}

			

		offset += (4 + olen);
	}
		
	if ( server == NULL || session == NULL)
		return(-1);

	server->last_packet_received.tv_sec = timestamp->tv_sec;
	server->last_packet_received.tv_usec = timestamp->tv_usec;
	session->last_received.tv_sec = timestamp->tv_sec;
	session->last_received.tv_usec = timestamp->tv_usec;
	session->type_last_received = packet->msg_type;
	stats = &server->stats;

	switch (packet->msg_type){
		case DHCPV6_ADVERTISE:
			if (is_ack) {
				session->state |= SOLICIT_ACK;
				stats->solicit_acks_received++;
			}
			else {
				session->state |= SOLICIT_NAK;
				stats->solicit_naks_received++;
			}

			dt = DELTATV((*timestamp), session->last_sent);
			stats->advertise_latency_avg += dt;
			if ( dt < stats->advertise_latency_min )
				stats->advertise_latency_min = dt;
			if ( dt > stats->advertise_latency_max )
				stats->advertise_latency_max = dt;

		break;
		case DHCPV6_REPLY:
			switch (session->state) {
				case SESSION_ALLOCATED|SOLICIT_SENT|SOLICIT_ACK|REQUEST_SENT:
				case SESSION_ALLOCATED|REQUEST_SENT:
					if (is_ack){
						session->state |= REQUEST_ACK;
						stats->request_acks_received++;
					}
					else {
						session->state |= REQUEST_NAK;
						stats->request_naks_received++;
					}
				break;
				case SESSION_ALLOCATED|RAPID_SOLICIT_SENT:
					if (is_ack){
						session->state |= SOLICIT_ACK;
						stats->request_acks_received++;
					}
					else {
						session->state |= SOLICIT_NAK;
						stats->request_naks_received++;
					}
				break;
				case SESSION_ALLOCATED|RENEW_SENT:
					if (is_ack){
						session->state |= RENEW_ACK;
						stats->renew_acks_received++;
					}
					else {
						session->state |= RENEW_NAK;
						stats->renew_naks_received++;
					}
				break;
				case SESSION_ALLOCATED|SOLICIT_SENT|SOLICIT_ACK|REQUEST_SENT|REQUEST_ACK|INFORM_SENT:
				case SESSION_ALLOCATED|INFORM_SENT:
					if (is_ack){
						session->state |= INFORM_ACK;
						stats->inform_acks_received++;
					}
					else {
						session->state |= INFORM_NAK;
						stats->inform_naks_received++;
					}
				break;
				case SESSION_ALLOCATED|SOLICIT_SENT|SOLICIT_ACK|REQUEST_SENT|REQUEST_ACK|DECLINE_SENT:
				case SESSION_ALLOCATED|DECLINE_SENT:
					if (is_ack){
						session->state |= DECLINE_ACK;
						stats->decline_acks_received++;
					}
					else {
						session->state |= DECLINE_NAK;
						stats->decline_naks_received++;
					}
				break;
				case SESSION_ALLOCATED|SOLICIT_SENT|SOLICIT_ACK|REQUEST_SENT|REQUEST_ACK|RELEASE_SENT:
				case SESSION_ALLOCATED|RAPID_SOLICIT_SENT|SOLICIT_ACK|RELEASE_SENT:
				case SESSION_ALLOCATED|RELEASE_SENT:
					if (is_ack){
						session->state |= RELEASE_ACK;
						stats->release_acks_received++;
					}
					else {
						session->state |= RELEASE_NAK;
						stats->release_naks_received++;
					}
				break;
				case SESSION_ALLOCATED|SOLICIT_SENT|SOLICIT_ACK|REQUEST_SENT|REQUEST_ACK|REBIND_SENT:
				case SESSION_ALLOCATED|REBIND_SENT:
					if (is_ack){
						session->state |= REBIND_ACK;
						stats->rebind_acks_received++;
					}
					else {
						session->state |= REBIND_NAK;
						stats->rebind_naks_received++;
					}
				break;
				case SESSION_ALLOCATED|CONFIRM_SENT:
					if (is_ack){
						session->state |= CONFIRM_ACK;
						stats->confirm_acks_received++;
					}
					else {
						session->state |= CONFIRM_NAK;
						stats->confirm_naks_received++;
					}
				break;
				default:
					fprintf(logfp, "PP: Unknown session state: %d\n", session->state);
					decode_state(session->state);
				break;
			}

			dt = DELTATV((*timestamp), session->last_sent);
			stats->reply_latency_avg += dt;
			if ( dt < stats->reply_latency_min )
				stats->reply_latency_min = dt;
			if ( dt > stats->reply_latency_max )
				stats->reply_latency_max = dt;

			break;
		default:
			fprintf(logfp,"Unknown DHCP type: %d\n", packet->msg_type);
			session->state = PACKET_ERROR;
			return(-1);
	}

	
	/* remember last packet for next possible reuse */
	//memcpy(session->last_packet, packet, length);
	//session->last_packet_len = length;

	if (verbose)
		print_packet(length, packet, "Recv: ");

	return(0);
}

void parse_args(int argc, char **argv)
{
	char		ch;
	int		i;
	uint64_t	val;
	int		temp[6];
	char		*tokes[32];

	if ( argc < 3)
		usage();

	while ((ch = getopt(argc, argv, "a:Ac:ed:D:f:h:H:i:I:l:mn:No:O:pPq:rR:s:S:t:u:vz")) != -1){
		switch (ch) {
           	case 'a':
                	if (strchr(optarg, ':') == NULL) {
                			val = atoll(optarg);
                  			for (i = 0; i < 6; i++)
                  				firstmac[5 - i] = (val & (0x00ffULL << i * 8)) >> i * 8;
                  		}
                		else {
                			if (sscanf(optarg, "%2x:%2x:%2x:%2x:%2x:%2x",
                			temp, temp + 1, temp + 2, temp + 3, temp + 4, temp + 5) < 6) {
                 			fprintf(stderr, "\nBad MAC Address in -a option: %s\n", optarg);
                 			usage();
                 		}
                  		for (i = 0; i < 6; i++)
                  			firstmac[i] = (char) temp[i];
                  	}
                  	use_sequential_mac = 1;
                  	break;
		case 'A':
			fprintf(stderr,"Will simulate relay agent\n");
			use_relay = 1;
			break;
		case 'c':
                        if ( inet_pton(AF_INET6, optarg, &srcaddr) == 0 ){
                                fprintf(stderr,"Invalid source IPV6 address %s\n",
                                        optarg);
                                exit(1);
                        }
                        break;
                case 'd':
			send_delay = atol(optarg);
			break;
                case 'D':
			update_domain = strdup(optarg);
			fprintf(stderr,"Using %s for DDNS updates\n", update_domain);
			break;
		case 'e':
			send_decline = 1;
			break;
		case 'f':
			input_file = strdup(optarg);
			break;
		case 'h':
			if (*optarg == 'N' || *optarg == 'n')
				server_should_ddns = 0;
			send_hostname = 1;
			break;
		case 'H':
			if (*optarg == 'N' || *optarg == 'n')
				server_should_ddns = 0;
			random_hostname = 1;
			send_hostname = 1;
			break;
		case 'i':
			if (add_servers(optarg) < 0)
				exit(1);
			break;
		case 'I':
		        nrequests = get_tokens(optarg, tokes, MAX_TOKENS);
			info_requests = malloc(nrequests * sizeof(uint16_t));
			assert(info_requests);
			for (i=0; i < nrequests; i++)
				info_requests[i] = htons(atoi(tokes[i]));
			break;
		case 'l':
			logfile = strdup(optarg);
			break;
		case 'm':
                	use_sequential_mac = 1;
                	break;
		case 'n':
			number_requests = atol(optarg);
			break;
		case 'N':
			send_until_answered = 1;
			break;
		case 'o':
			output_file = strdup(optarg);
			break;
		case 'O':
			{
				int option;
				char *s;

				option = strtol(optarg, &s, 0);
				if (option == 0xff || s[0] != ':' || s[1] == '\0')
					usage();

				if (addoption(option, s + 1) < 0)
					usage();
			}
			break;
		case 'p':
			dhcp_ping = 1;
			break;
		case 'P':
			request_prefix = 1;
			break;
		case 'q':
			max_sessions = atol(optarg);
			break;
		case 'r':
			send_release = 1;
			break;
		case 'R':
			retransmit = atol(optarg);
			break;
		case 's':
			if (!strcasecmp(optarg,"solicit"))
				start_from = DHCPV6_SOLICIT;
			else if (!strcasecmp(optarg,"request"))
				start_from = DHCPV6_REQUEST;
			else if (!strcasecmp(optarg,"confirm"))
				start_from = DHCPV6_CONFIRM;
			else if (!strcasecmp(optarg,"renew"))
				start_from = DHCPV6_RENEW;
			else if (!strcasecmp(optarg,"rebind"))
				start_from = DHCPV6_REBIND;
			else if (!strcasecmp(optarg,"release"))
				start_from = DHCPV6_RELEASE;
			else if (!strcasecmp(optarg,"decline"))
				start_from = DHCPV6_DECLINE;
			else if (!strcasecmp(optarg,"inform"))
				start_from = DHCPV6_INFORMATION_REQUEST;
			else {
				fprintf(stderr,"Unknown start_from: %s\n", optarg);
				usage();
			}
			break;
		case 'S':
			if (parse_opt_seq(optarg) < 0)
				usage();
			opt_seq=1;
			break;
		case 't':
			timeout = 1000*atol(optarg);
			break;
		case 'u':
			num_per_mac = atol(optarg);
			break;
		case 'v':
			verbose = 1;
			break;
/*
		case 'w':
			renew_lease =1;
			start_from = DHCPV6_RENEW;
			break;
*/
		case 'z':
			rapid_commit=1;
			break;
		case '?':
		default:
                     usage();
		}
	}
/*
	if (send_info_request && (renew_lease || send_decline || send_release)) {
		fprintf(stderr, "Information-Request doesn't grab IP address\n");
		usage();
	}
*/
	/* require server IP address and lease file */
	if (servers == NULL){
		fprintf(stderr, "No servers defined. Using FF05::1:3\n");
		add_servers("ff05::1:3");
		//usage();
	}
}

int add_servers(const char *s)
{
	const char *p1 = s;
	dhcp_server_t *sp = NULL;

	char buffer[INET6_ADDRSTRLEN];
	int len;

	assert(s != NULL);
	while (*p1 != '\0'){
		while (*p1 != ',' && *p1 != '\0')
			p1++;
		len = p1 - s;
		if (len == 0 || len + 1 > sizeof(buffer))
			return(-1);
		strncpy(buffer, s, len);
		s = p1+1;
		buffer[len]='\0';

		if ( *p1 != '\0')
			p1++;

		sp = malloc(sizeof(dhcp_server_t));
		assert(sp);
		memset(sp,'\0', sizeof(dhcp_server_t));
		sp->stats.advertise_latency_min=10000000;
		sp->stats.reply_latency_min=10000000;
		sp->sa.sin6_port = htons(DHCP6_SERVER_PORT);
		sp->sa.sin6_family = AF_INET6;
		if (inet_pton(AF_INET6, buffer, &sp->sa.sin6_addr) == 0){
			fprintf(stderr,"Invalid address %s\n", buffer);
			return(-1);
		}
		sp->next = servers;
		servers = sp;
		num_servers++;
	}
	return(0);
}

void print_packet(uint32_t packet_len, struct dhcpv6_packet *p, const char *prefix)
{

	int i = 0, option_len = 0, suboption_len = 0;
	uint16_t option_code = 0, suboption_code = 0;

	fprintf(logfp, "%sPacket Type %d (%s), len = %d, ", prefix, p->msg_type, typestrings[p->msg_type-1], packet_len);
	fprintf(logfp, "txn: [%d %d %d]\n", p->transaction_id[0], p->transaction_id[1], p->transaction_id[2]);

	unsigned char *option = p->options;
	while (option < p->options + packet_len - 4)
	{
		option_code = ntohs(*((uint16_t *) option));
		assert(option_code <= sizeof(optionstrings));

		option_len = ntohs(*((uint16_t *) (option + 2)));

		fprintf(logfp, "   Option %d (%s), len = %d [", option_code, optionstrings[option_code-1], option_len);
		for (i = 0; i < option_len; i++)
			fprintf(logfp, " %d", option[i + 4]);

		fprintf(logfp, " ]\n");

		/* expand IA_NA/IA_TA/IA_PD options */
		if (option_code == D6O_IA_NA || option_code == D6O_IA_TA || option_code == D6O_IA_PD) {
			unsigned char *suboption = option + 16;
			//unsigned char *suboption = option + 8;
			while (suboption < option + option_len + 4) {
				suboption_code = ntohs(*((uint16_t *) suboption));
				suboption_len = ntohs(*((uint16_t *) (suboption + 2)));
				if (!suboption_code) {
					suboption += 4;
					continue;
				}
				fprintf(logfp, "      Option %d (%s), len = %d [", 
					suboption_code, optionstrings[suboption_code-1], suboption_len);
				for (i = 0; i < suboption_len; i++)
					fprintf(logfp, " %d", suboption[i + 4]);
				fprintf(logfp, " ]\n");

				suboption += suboption_len + 4;
			}
		}

		/* option size + option-len size */
		option += option_len + 4;
	}
	fprintf(logfp, "\n");
}

#define MAX_CLIENTID_LEN 100
uint32_t read_lease_data(lease_data_t **leases)
{
	char		buf[1024];
	int		ret, i;
	int		ntokes;
	char		*tokes[MAX_TOKENS];
	char		*cp;
	int		temp[6];
	uint8_t		mac[6], prefix_len;
	uint32_t	read_count = 0;
	uint32_t	iaid=0;
	struct in6_addr	ipaddr;
	uint8_t		a,b;
	int		lineno = 0;
	lease_data_t	*lease = *leases;
	FILE		*fpin;

        fpin = fopen(input_file, "r");
	if (fpin == NULL){
		fprintf(logfp,"Could not open input file: %s\n",input_file);
		return(0);
	}
	while ( fgets(buf, sizeof(buf), fpin) != NULL){
		lineno++;
		if ((ntokes = get_tokens(buf, tokes, MAX_TOKENS)) < 3){
			fprintf(logfp, "Too few tokens: %d, line %d\n", ntokes,lineno);
			continue;
		}

           	ret = sscanf(tokes[0], "%2x:%2x:%2x:%2x:%2x:%2x", temp,
                                 temp + 1, temp + 2, temp + 3, temp + 4, temp + 5);
           	if (ret < 6) {
                  	fprintf(logfp, "Line %d, MAC format error: %s\n", lineno, tokes[0]);
                  	continue;
           	}
           	for (i = 0; i < 6; i++)
                  	mac[i] = (char) temp[i];

	   	ret = sscanf(tokes[1], "%u", &iaid);
           	if (ret < 1) {
                  	fprintf(logfp, "Line %d, IAID format error: %s\n", lineno, tokes[1]);
                  	continue;
           	}

		prefix_len=0;
		if ((cp=strchr(tokes[2], '/')) != NULL){
			prefix_len = atoi(cp+1);
			*cp ='\0';
		}
			
           	if (inet_pton(AF_INET6, tokes[2], &ipaddr) <= 0) {
                  	if (*tokes[2] == '-')
                         	ipaddr = in6addr_any;
                  	else {
                         	fprintf(logfp, "Line %d, format error: %s\n", lineno, tokes[2]);
                         	continue;
                  	}
           	}

           	if (*leases == NULL) {
                  	*leases = calloc(1, sizeof(lease_data_t));
                  	lease = *leases;
           	}
           	else {
                  	lease->next = calloc(1, sizeof(lease_data_t));
                  	lease = lease->next;
           	}
           	assert(lease != NULL);

           	lease->ia[0].ipaddr = ipaddr;
           	lease->ia[0].prefix_len = prefix_len;
           	lease->ia[0].iaid = iaid;
		lease->num_ia = 1;
           	memcpy(lease->mac, mac, 6);
           	lease->next = NULL;
	
	   	if (ntokes > 3){
			lease->serverid_len = strlen(tokes[3]);
			if ( lease->serverid_len % 2){
				fprintf(stderr, "\tInvalid Server DUID: %s\n",
							tokes[3]);
				exit(1);
			}
			lease->serverid_len /= 2;
	          	for (i=0; i < lease->serverid_len; i++){
			  	a = *(tokes[3] + 2*i);
			  	b = *(tokes[3] + 2*i +1);
			  	if ( a >= '0' && a <= '9')
					a = a - '0';
			  	else 
					a = 10 + a - 'a';
	
			  	if ( b >= '0' && b <= '9')
					b = b - '0';
			  	else 
					b = 10 + b - 'a';
		          	lease->serverid[i] = a * 16 + b;
		  	}
    	   	}
	   	else
			lease->serverid[0] = 0xff;

           	if (ntokes > 4 && *tokes[4] != '-')
                  	lease->hostname = strdup(tokes[4]);
           	else
                  	lease->hostname = NULL;

           	if (ntokes > 5 && *tokes[5] != '-')
           		inet_pton(AF_INET6, tokes[5], &lease->sa);
           	else
                  	lease->sa = in6addr_any;

	    	read_count++;
		/* Possibly Multiple IAIDs in this entry */
		if (ntokes < 6)
			continue;
		for (i=6; i < ntokes - 3; i += 3){
			if (strncasecmp(tokes[i], "IA:", 3) == 0){
				lease->ia[lease->num_ia].iaid = atol(tokes[i+1]);
				lease->ia[lease->num_ia].prefix_len=0;
				if ((cp=strchr(tokes[i+2], '/')) != NULL){
					lease->ia[lease->num_ia].prefix_len = atoi(cp+1);
					*cp ='\0';
				}
           			if (inet_pton(AF_INET6, tokes[i+2], &lease->ia[lease->num_ia].ipaddr) <= 0) 
                         		fprintf(logfp, "Line %d, format error: %s\n", lineno, tokes[i+2]);
				else 
					lease->num_ia++;
                  	}
			else 
                         	fprintf(logfp, "Line %d, format error: %s\n", lineno, tokes[i]);
				
           	}
			

	}
	return(read_count);
}

struct in6_addr get_local_addr(void)
{

	int		lsock;
	struct		sockaddr_in6 ca;
	socklen_t	calen = sizeof(ca);
	char		ip6str[272];


	if ((lsock=socket(AF_INET6, SOCK_DGRAM, 0)) < 0 ){
		perror("socket:");
		exit(1);
	}
	if (servers == NULL){
		fprintf(logfp,"get_local_addr: servers list is NULL\n");
		exit(1);
	}
	if ( connect(lsock, (const  struct sockaddr *) &servers->sa,
						sizeof(servers->sa)) < 0 ){
		perror("get_local_addr: Connect");
		return(in6addr_any);
	}
	getsockname(lsock, (struct sockaddr *) &ca, &calen);
	close(lsock);
	fprintf(stderr, "get_local_addr: Using source address: %s\n", inet_ntop(AF_INET6, &ca.sin6_addr,
				ip6str, sizeof(ip6str)));
	return(ca.sin6_addr);
}


dhcp_session_t *find_free_session( dhcp_server_t *s)
{
	int		i;
	dhcp_session_t	*list = s->list;
	if ( s->active == max_sessions)
		return(NULL);
	for (i=0; i < max_sessions; i++){
		if  (list[i].state == UNALLOCATED){
			list[i].state = SESSION_ALLOCATED;
			s->active++;
			return(list+i);
		}
	}
	return(NULL);
}

void reader(void)
{
	static uint8_t		buffer[1024];
	ssize_t			packet_length;
	int			pollto=20;
	struct timeval		timestamp;
	struct pollfd		fds={sock,POLLIN,0};


	while (poll(&fds, 1, pollto) >  0){
		packet_length = recv(sock, buffer, sizeof(buffer), 0);
#ifdef linux
		ioctl(sock, SIOCGSTAMP, &timestamp);
#else
		gettimeofday(&timestamp, NULL);
#endif
		if (packet_length < 0 ){
			fprintf(logfp, "Packet receive error\n");
			return;
		}
		pollto = 0;
		//printf("Packet length %d\n", packet_length);
		process_packet(buffer, &timestamp, packet_length);
	}
	if (fds.revents & (POLLERR|POLLNVAL)) {
		fprintf(stderr,"reader: poll socket error\n");
	}
	return;
}

int addoption(int option_no, char *hexdata)
{
    int i;
    int datalen = strlen(hexdata);
    char *data;
    struct option_st *opt;

    if (datalen % 2 != 0) {
	fprintf(stderr, "Odd number of hex chars\n");
	return -1;
    }

    datalen /= 2;
    data = malloc(datalen);

    if (strspn(hexdata, "0123456789abcdefABCDEF") != datalen * 2
	|| datalen > 255)
    {
	fprintf(stderr, "Illegal hex chars or too long (max 255)\n");
	return -1;
    }

    nextraoptions++;

    extraoptions = realloc(extraoptions,
			   nextraoptions * sizeof(struct option_st *));

    opt = malloc(sizeof(struct option_st));

    extraoptions[nextraoptions - 1] = opt;

    opt->option_no = option_no;
    opt->data_len = datalen;
    opt->data = data;

    for (i = 0; hexdata[i]; i += 2) {
	int x;

	sscanf(hexdata + i, "%2x", &x);

	*data++ = (char)(x & 0377);
    }

    return 0;
}
void getmac(uint8_t * s)
{
    int carry = 5;
    int i1, i2;

    if (use_sequential_mac) {
           if (s)
                  memcpy(s, firstmac, 6);
           while (carry >= 0) {
                  if (firstmac[carry] == 0xFF)
                         firstmac[carry--] = 0;
                  else {
                         firstmac[carry]++;
                         return;
                  }
           }
           return;
    }
    if (s) {
           i1 = rand();
           i2 = rand();
           memcpy(s, &i1, 4);
           memcpy(s + 4, &i2, 2);
    }
}
int get_tokens(char *cp, char **tokes, int maxtokes)
{
    int n = 0;
    if (cp == NULL)
           return (0);
    /* Eat any leading whitespace */
    while (*cp == ' ' && cp != '\0')
           cp++;

    if (*cp == '\0')
           return (0);
    tokes[0] = cp;
    while (*cp != '\0' && *cp != '\n') {
           if (*cp == ' ' || *cp == '\t') {
                  *(cp++) = '\0';
                  while (*cp != '\0' && (*cp == ' ' || *cp == '\t'))
                         cp++;
                  if (++n < maxtokes)
                         tokes[n] = cp;
                  else
                         return (0);
           }
           else
                  cp++;
    }
    *cp = '\0';
    return (n + 1);
}
void print_lease( FILE *fp, dhcp_session_t *session, struct in6_addr *sa)
{

	int i;

        static char addr[INET6_ADDRSTRLEN];
        fprintf(fp,
                "%02x:%02x:%02x:%02x:%02x:%02x %u ",
                session->mac[0], session->mac[1],
                session->mac[2], session->mac[3],
                session->mac[4], session->mac[5], session->ia[0].iaid);

	if (memcmp(&session->ia[0].ipaddr, &in6addr_any, sizeof(struct in6_addr))){
        	fprintf(fp,"%s", inet_ntop(AF_INET6, &session->ia[0].ipaddr, addr, INET6_ADDRSTRLEN));
		if (session->ia[0].prefix_len)
        		fprintf(fp,"/%u ", session->ia[0].prefix_len);
		else
			fputc(' ', fp);
	}
	else
		fputs("- ", fp);

	if (session->serverid[0] != 0xff){
		for (i=0; i < session->serverid_len; i++)
			fprintf(fp, "%02x", session->serverid[i]);
	}

	if (session->hostname[0])
		fprintf(fp, " %s ", session->hostname);
	else 
		fprintf(fp, " - ");

	if (memcmp(sa, &in6addr_any, sizeof(struct in6_addr)))
		fprintf(fp, " %s ", inet_ntop(AF_INET6, sa, addr, INET6_ADDRSTRLEN));
	else
		fprintf(fp, " - ");

	//fprintf(stderr,"print_lease: Received IA: %u\n", session->recv_ia);
	if (session->recv_ia > 1){
		for (i=1; i < session->recv_ia; i++){
			fprintf(fp, "IA: %u %s", session->ia[i].iaid,
				inet_ntop(AF_INET6, &session->ia[i].ipaddr, addr, INET6_ADDRSTRLEN));
			if (session->ia[i].prefix_len)
        			fprintf(fp,"/%u ", session->ia[0].prefix_len);
			else
				fputc(' ', fp);
		}
	}
	fputc('\n', fp);		
                
}
/* I guess this can be an FQDN or unqualified hostname */
int pack_client_fqdn(uint8_t *options, char *hostname)
{
	int offset=0;
	*((uint16_t *)(options + offset)) = htons(D6O_CLIENT_FQDN);
	offset += 4;
	if (server_should_ddns)
		options[offset] = 1; //S=1,O=0,N=0
	else
		options[offset] = 4; //S=1,O=0,N=0
	offset++;
	offset += encode_domain(hostname, options + offset);
	*((uint16_t *)(options + 2)) = htons(offset - 4);
	return(offset);
}
int encode_domain( char *domain, uint8_t *buf)
{
        uint8_t *plen;
        int len=1;

        plen = buf;
        buf++;
        *plen = 0;
        while (*domain != '\0'){
                if (*domain == '.'){
                        if (*(domain + 1) == '\0')
                                break;
                        else {
                                plen = buf++;
                                *plen = 0;
                                domain++;
                                len++;
                                continue;
                        }
                }
                *(buf++) = *(domain++);
                (*plen)++;
                len++;
        }
        *buf=0;
        return(len+1);
}
void usage()
{
	fprintf(stderr,
"Usage: dras6 -i <server IP> [-f <input-lease-file>] [-o <output-lease-file>]\n"
"	[-A] -O <dec option-no>:<hex data>] [-l <logfile>] [-t <timeout>] [-a <mac>]\n"
"	[-n <number requests>] [-q <max outstanding> [-R <retransmits>]\n"
"	[-d <delay>] [-c <relay agent IP>] [-e|mN|p|r|w] [-I <requested options>]\n"
"	[-s <renew|inform|confirm|decline|rebind>] [-S <sol|req|ren>,opno1,opno2,...]\n\n");

	fprintf(stderr,
"	-a Starting MAC address\n"
"	-A Simulate relay agent\n"
"	-d Delay before sending next packet\n"
"	-D DNS zone for CLIENT_FQDN option\n"
"	-e Send decline after ACK received\n"
"	-f Input file with ClientID/IPv6 leases\n"
"       Format:  <MAC> <IAID> <IPADDR|\"-\">[/<prefix>] <Server DUID> [<hostname|FQDN>]\n"
"       e.g.: 00:00:00:00:00:01 1 2001:db8:a22:1f00::64 0001000114da166c00259003467d h788047619869273952\n"
"	-h Add CLIENT_FQDN option\n"
"	-H Add CLIENT_FQDN optionwith random hostnames\n"
"	-i Server IP Address (multiple servers are separated by commas)\n"
"	-I List of option numbers to request (separated by spaces, in quotes)\n"
"	e.g.: -I \"11 34 22\"\n"
"	-l Output logfile (default: stderr)\n"
"	-m Start at MAC 0\n"
"	-n Number of requests\n"
"	-N Retransmit until answer received\n"
"	-o Output lease file\n"
"	-O Send option <option-no> (dec) with data <hex data> (w/o length)\n"
"	-p Ping mode: end after ADVERTISE received\n"
"	-P Prefix request (IAPREFIX)\n"
"	-q Maximum outstanding requests\n"
"	-r Send release after ACK received\n"
"	-R Number of retransmits (default 0)\n"
"	-s Start from RENEW|REQUEST|INFORM|CONFIRM with -f <file> option\n"
"	-t Timeout on requests (ms)\n"
"	-v Verbose output\n"
"	-z Use rapid commit option\n");

	exit(1);
}
void decode_state(uint32_t state)
{

	if ( state == 0){
		fprintf(logfp,"Unallocated\n");
		return;
	}
	if (state & SESSION_ALLOCATED)
		fprintf(logfp,"SESSION_ALLOCATED|");
	if (state & SOLICIT_SENT)
		fprintf(logfp,"SOLICIT_SENT|");
	if (state & RAPID_SOLICIT_SENT)
		fprintf(logfp,"RAPID_SOLICIT_SENT|");
	if (state & RENEW_SENT)
		fprintf(logfp,"RENEW_SENT|");
	if (state & REQUEST_SENT)
		fprintf(logfp,"REQUEST_SENT|");
	if (state & RELEASE_SENT)
		fprintf(logfp,"RELEASE_SENT|");
	if (state & DECLINE_SENT)
		fprintf(logfp,"DECLINE_SENT|");
	if (state & INFORM_SENT)
		fprintf(logfp,"INFORM_SENT|");
	if (state & REBIND_SENT)
		fprintf(logfp,"REBIND_SENT|");
	if (state & CONFIRM_SENT)
		fprintf(logfp,"CONFIRM_SENT|");
	if (state & SOLICIT_ACK)
		fprintf(logfp,"SOLICIT_ACK|");
	if (state & SOLICIT_NAK)
		fprintf(logfp,"SOLICIT_NAK|");
	if (state & REQUEST_ACK)
		fprintf(logfp,"REQUEST_ACK|");
	if (state & REQUEST_NAK)
		fprintf(logfp,"REQUEST_NAK|");
	if (state & RENEW_ACK)
		fprintf(logfp,"RENEW_ACK|");
	if (state & RENEW_NAK)
		fprintf(logfp,"RENEW_NAK|");
	if (state & REBIND_ACK)
		fprintf(logfp,"REBIND_ACK|");
	if (state & REBIND_NAK)
		fprintf(logfp,"REBIND_NAK|");
	if (state & RELEASE_ACK)
		fprintf(logfp,"RELEASE_ACK|");
	if (state & RELEASE_NAK)
		fprintf(logfp,"RELEASE_NAK|");
	if (state & DECLINE_ACK)
		fprintf(logfp,"DECLINE_ACK|");
	if (state & DECLINE_NAK)
		fprintf(logfp,"DECLINE_NAK|");
	if (state & INFORM_ACK)
		fprintf(logfp,"INFORM_ACK|");
	if (state & INFORM_NAK)
		fprintf(logfp,"INFORM_NAK|");
	if (state & CONFIRM_ACK)
		fprintf(logfp,"CONFIRM_ACK|");
	if (state & CONFIRM_NAK)
		fprintf(logfp,"CONFIRM_NAK|");
	if (state & PACKET_ERROR)
		fprintf(logfp,"PACKET_ERROR|");
	fputs("\n", logfp);
	return;
}
int parse_opt_seq(char *s)
{
	
	char *cp;
	uint16_t  *seqptr=NULL;
	if ((cp=strtok(s,", ")) == NULL){
		printf("Syntax error opt seq: %s\n", s);
		return(-1);
	}
	while (cp != NULL){
		if (*cp >= '0' && *cp <= '9'){
			if (seqptr == NULL){
				printf("Need to define packet type (sol|req|ren) in option sequence\n");
				return(-1);
			}

			*seqptr = atoi(cp);

			if (*seqptr == D6O_CLIENT_FQDN){
				server_should_ddns = 0;
				random_hostname = 1;
				send_hostname = 1;
			}

			seqptr++;
			
		}
		else {
			if (strncasecmp(cp,"sol",3) == 0)
				seqptr=sol_optseq;
			else if (strncasecmp(cp,"req",3) == 0)
				seqptr=req_optseq;
			else if (strncasecmp(cp,"ren",3) == 0)
				seqptr=ren_optseq;
			else {
				printf("Bad token in DHCP option sequence: %s\n", cp);
				return(-1);
			}
			while (*seqptr)
				seqptr++;
		}
		cp=strtok(NULL,", ");
		
	}
	printf("\nSolicit Options: ");
	if (*sol_optseq){
		for (seqptr=sol_optseq; *seqptr; seqptr++)
			printf("%u ", *seqptr);
	}
	if (*req_optseq){
		printf("\nRequest Options: ");
		for (seqptr=req_optseq; *seqptr; seqptr++)
			printf("%u ", *seqptr);
	}
	if (*ren_optseq){
		printf("\nRenew Options: ");
		for (seqptr=ren_optseq; *seqptr; seqptr++)
			printf("%u ", *seqptr);
		printf("\n");
	}		
	return(0);
}
int add_opt_seq(uint8_t  type, uint8_t *options, dhcp_session_t *session)
{
	int	offset=0;
	int	i, found;
	uint16_t *seqptr;

	if (type == DHCPV6_SOLICIT)
		seqptr = sol_optseq;
	else if (type == DHCPV6_RENEW)
		seqptr = ren_optseq;
	else
		seqptr = req_optseq;

	for (; *seqptr; seqptr++){
		printf("Adding option %u\n", *seqptr);
		// See if user defined the option
		if (nextraoptions) {
			found=0;
			for (i=0; i < nextraoptions; i++){
				if (extraoptions[i]->option_no == *seqptr){
					*((uint16_t *) (options + offset)) = htons(*seqptr);
					offset += 2;
					*((uint16_t *) (options + offset)) = htons(extraoptions[i]->data_len);
					offset += 2;
					memcpy(options + offset, extraoptions[i]->data, extraoptions[i]->data_len);
					offset += extraoptions[i]->data_len;
					found=1;
					break;
				}
				
			}
			if (found)
				continue;
		}
		switch (*seqptr){
		   case D6O_CLIENTID:
 			/* CLIENT_ID option */
        		*((uint16_t *) (options + offset)) = htons(D6O_CLIENTID);
        		offset += 2;
        		*((uint16_t *) (options + offset)) = htons(DUID_LLT_LEN);
        		offset += 2;
        		*((uint16_t *) (options + offset)) = htons(1);
        		offset += 2;
        		*((uint16_t *) (options + offset)) = htons(1);
        		offset += 2;
        		memset(options + offset, 0x88, 4);
        		offset += 4;
        		memcpy(options + offset, session->mac, 6);
        		offset += 6;
			break;
		   case D6O_STATUS_CODE:
        		*((uint16_t *) (options + offset)) = htons(D6O_STATUS_CODE);
        		offset += 2;
        		*((uint16_t *) (options + offset)) = htons(2);
        		offset += 2;
        		*((uint16_t *) (options + offset)) = htons(STATUS_Success);
        		offset += 2;
			break;
		   case D6O_RAPID_COMMIT:
			 *((uint16_t *) (options + offset)) = htons(D6O_RAPID_COMMIT);
                	offset += 2;
                	*((uint16_t *) (options + offset)) = 0;
                	offset += 2;
			break;
		   case D6O_SERVERID:
			if (session->serverid[0] == 0xff)
				break;
                	*((uint16_t *) (options + offset)) = htons(D6O_SERVERID);
                	offset += 2;
                	*((uint16_t *) (options + offset)) =
                                        	htons(session->serverid_len);
                	offset += 2;
                	memcpy(options + offset, session->serverid,
                        	session->serverid_len);
                	offset += session->serverid_len;
			break;
		   case D6O_CLIENT_FQDN:
			if (session->hostname[0] == '\0')
				break;
			offset += pack_client_fqdn(options + offset, session->hostname);
			break;
		   case D6O_ELAPSED_TIME:
                	*((uint16_t *) (options + offset)) = htons(D6O_ELAPSED_TIME);
                	offset += 2;
                	*((uint16_t *) (options + offset)) = htons(2);//Does this matter?
                	offset += 2;
                	*((uint16_t *) (options + offset)) = htons(42);//Does this matter?
                	offset += 2;
			break;
		   case D6O_IA_NA: 
			offset += fill_iafu_mess(session, options + offset);
			break;	
		   case D6O_ORO:
			*((uint16_t *) (options + offset)) = htons(D6O_ORO);
			offset += 2;
			*((uint16_t *) (options + offset)) = htons(8);
			offset += 2;
			*((uint16_t *) (options + offset)) = htons(24);
			offset += 2;
			*((uint16_t *) (options + offset)) = htons(23);
			offset += 2;
			*((uint16_t *) (options + offset)) = htons(17);
			offset += 2;
			*((uint16_t *) (options + offset)) = htons(39);
			offset += 2;
			break;
		   case D6O_VENDOR_CLASS:
			*((uint16_t *) (options + offset)) = htons(D6O_VENDOR_CLASS);
			offset += 2;
			*((uint16_t *) (options + offset)) = htons(14);
			offset += 2;
			*((uint32_t *) (options + offset)) = htonl(311);
			offset += 4;
			*((uint16_t *) (options + offset)) = 0x0008;
			offset +=2;
			memcpy(options+offset, "MSFT 5.0", 8);
			offset += 8;
			break;
		   default:
			fprintf(stderr,"Can't add option number %u\n", *seqptr);
			break;
		}
	}
	return(offset);

}
