/*
NetJack Client

Copyright (C) 2008 Marc-Olivier Barre <marco@marcochapeau.org>
Copyright (C) 2008 Pieter Palmers <pieterpalmers@users.sourceforge.net>
Copyright (C) 2006 Torben Hohn <torbenh@gmx.de>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

/** @file netsource.c
 *
 * @brief This client connects a remote slave JACK to a local JACK server assumed to be the master
 */

#include "config.h" 
 
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <netinet/in.h>
#include <netdb.h>

/* These two required by FreeBSD. */
#include <sys/types.h>
#include <sys/socket.h>

#if HAVE_ALLOCA_H
#include <alloca.h>
#endif

#include <jack/jack.h>

#include <net_driver.h>
#include <netjack_packet.h>
#include <samplerate.h>

#ifdef HAVE_CELT
#include <celt/celt.h>
#endif

JSList *capture_ports = NULL;
JSList *capture_srcs = NULL;
int capture_channels = 0;
int capture_channels_audio = 2;
int capture_channels_midi = 1;
JSList *playback_ports = NULL;
JSList *playback_srcs = NULL;
int playback_channels = 0;
int playback_channels_audio = 2;
int playback_channels_midi = 1;

int latency = 5;
jack_nframes_t factor = 1;
int bitdepth = 0;
int mtu = 1400;
int reply_port = 0;
jack_client_t *client;

int state_connected = 0;
int state_latency = 0;
int state_netxruns = 0;
int state_currentframe = 0;


int outsockfd;
int insockfd;
struct sockaddr destaddr;
struct sockaddr bindaddr;

int sync_state;
jack_transport_state_t last_transport_state;

int framecnt = 0;

int cont_miss = 0;

/**
 * This Function allocates all the I/O Ports which are added the lists.
 */
void
alloc_ports (int n_capture_audio, int n_playback_audio, int n_capture_midi, int n_playback_midi)
{

    int port_flags = JackPortIsOutput;
    int chn;
    jack_port_t *port;
    char buf[32];

    capture_ports = NULL;
    /* Allocate audio capture channels */
    for (chn = 0; chn < n_capture_audio; chn++)
    {
        snprintf (buf, sizeof (buf) - 1, "capture_%u", chn + 1);
        port = jack_port_register (client, buf, JACK_DEFAULT_AUDIO_TYPE, port_flags, 0);
        if (!port)
        {
            printf( "jack_netsource: cannot register %s port\n", buf);
            break;
        }
	if( bitdepth == 1000 ) {
#ifdef HAVE_CELT
	    // XXX: memory leak
	    CELTMode *celt_mode = celt_mode_create( jack_get_sample_rate( client ), 1, jack_get_buffer_size(client), NULL );
	    capture_srcs = jack_slist_append(capture_srcs, celt_decoder_create( celt_mode ) );
#endif
	} else {
	    capture_srcs = jack_slist_append (capture_srcs, src_new (SRC_LINEAR, 1, NULL));
	}
        capture_ports = jack_slist_append (capture_ports, port);
    }

    /* Allocate midi capture channels */
    for (chn = n_capture_audio; chn < n_capture_midi + n_capture_audio; chn++)
    {
        snprintf (buf, sizeof (buf) - 1, "capture_%u", chn + 1);
        port = jack_port_register (client, buf, JACK_DEFAULT_MIDI_TYPE, port_flags, 0);
        if (!port)
        {
            printf ("jack_netsource: cannot register %s port\n", buf);
            break;
        }
        capture_ports = jack_slist_append(capture_ports, port);
    }

    /* Allocate audio playback channels */
    port_flags = JackPortIsInput;
    playback_ports = NULL;
    for (chn = 0; chn < n_playback_audio; chn++)
    {
        snprintf (buf, sizeof (buf) - 1, "playback_%u", chn + 1);
        port = jack_port_register (client, buf, JACK_DEFAULT_AUDIO_TYPE, port_flags, 0);
        if (!port)
        {
            printf ("jack_netsource: cannot register %s port\n", buf);
            break;
        }
	if( bitdepth == 1000 ) {
#ifdef HAVE_CELT
	    // XXX: memory leak
	    CELTMode *celt_mode = celt_mode_create( jack_get_sample_rate (client), 1, jack_get_buffer_size(client), NULL );
	    playback_srcs = jack_slist_append(playback_srcs, celt_encoder_create( celt_mode ) );
#endif
	} else {
	    playback_srcs = jack_slist_append (playback_srcs, src_new (SRC_LINEAR, 1, NULL));
	}
	playback_ports = jack_slist_append (playback_ports, port);
    }

    /* Allocate midi playback channels */
    for (chn = n_playback_audio; chn < n_playback_midi + n_playback_audio; chn++)
    {
        snprintf (buf, sizeof (buf) - 1, "playback_%u", chn + 1);
        port = jack_port_register (client, buf, JACK_DEFAULT_MIDI_TYPE, port_flags, 0);
        if (!port)
        {
            printf ("jack_netsource: cannot register %s port\n", buf);
            break;
        }
        playback_ports = jack_slist_append (playback_ports, port);
    }
}

/**
 * The Sync callback... sync state is set elsewhere...
 * we will see if this is working correctly.
 * i dont really believe in it yet.
 */
int
sync_cb (jack_transport_state_t state, jack_position_t *pos, void *arg) 
{
    static int latency_count = 0;
    int retval = sync_state;

    if (latency_count) {
        latency_count--;
        retval = 0;
    }

    else if (state == JackTransportStarting && last_transport_state != JackTransportStarting)
    {
        retval = 0;
        latency_count = latency - 1;
    }

    last_transport_state = state;
    return retval;
}

/**
 * The process callback for this JACK application.
 * It is called by JACK at the appropriate times.
 */
int
process (jack_nframes_t nframes, void *arg)
{
    jack_nframes_t net_period;

    if( bitdepth == 1000 )
	net_period = factor;
    else
	net_period = (float) nframes / (float) factor;

    int rx_bufsize =  get_sample_size (bitdepth) * capture_channels * net_period + sizeof (jacknet_packet_header);
    int tx_bufsize =  get_sample_size (bitdepth) * playback_channels * net_period + sizeof (jacknet_packet_header);

    jack_default_audio_sample_t *buf;
    jack_port_t *port;
    JSList *node;
    channel_t chn;
    int size, i;
    const char *porttype;

    jack_position_t local_trans_pos;

    uint32_t *packet_buf, *packet_bufX;

    /* Allocate a buffer where both In and Out Buffer will fit */
    packet_buf = alloca ((rx_bufsize > tx_bufsize) ? rx_bufsize : tx_bufsize);

    jacknet_packet_header *pkthdr = (jacknet_packet_header *) packet_buf;

    packet_bufX = packet_buf + sizeof (jacknet_packet_header) / sizeof (uint32_t);

    // New Receive Code:
    if (reply_port)
        packet_cache_drain_socket(global_packcache, insockfd);
    else
        packet_cache_drain_socket(global_packcache, outsockfd);

    size = packet_cache_retreive_packet( global_packcache, framecnt - latency, (char *)packet_buf, rx_bufsize ); 

    /* First alternative : we received what we expected. Render the data
     * to the JACK ports so it can be played. */
    if (size == rx_bufsize)
    {
        if (cont_miss)
        {
            //printf("Frame %d  \tRecovered from dropouts\n", framecnt);
            cont_miss = 0;
        }
        render_payload_to_jack_ports (bitdepth, packet_bufX, net_period, capture_ports, capture_srcs, nframes);

        /* Now evaluate packet header */
        //if (sync_state != pkthdr->sync_state)
        //    printf ("Frame %d  \tSync has been set\n", framecnt);

	state_currentframe = framecnt;
	//state_latency = framecnt - pkthdr->framecnt;
	state_connected = 1;
        sync_state = pkthdr->sync_state;
    }
    /* Second alternative : we've received something that's not
     * as big as expected or we missed a packet. We render silence
     * to the ouput ports */
    else
    {
	jack_nframes_t latency_estimate;
	if( packet_cache_find_latency( global_packcache, framecnt, &latency_estimate ) )
		state_latency = latency_estimate;

	// Set the counters up.
	state_currentframe = framecnt;
	//state_latency = framecnt - pkthdr->framecnt;
	state_netxruns += 1;

        //printf ("Frame %d  \tPacket missed or incomplete (expected: %d bytes, got: %d bytes)\n", framecnt, rx_bufsize, size);
        //printf ("Frame %d  \tPacket missed or incomplete\n", framecnt);
        cont_miss += 1;
        chn = 0;
        node = capture_ports;
        while (node != NULL)
        {
            port = (jack_port_t *) node->data;
            buf = jack_port_get_buffer (port, nframes);
            porttype = jack_port_type (port);
            if (strncmp (porttype, JACK_DEFAULT_AUDIO_TYPE, jack_port_type_size ()) == 0)
                for (i = 0; i < nframes; i++)
                    buf[i] = 0.0;
            else if (strncmp (porttype, JACK_DEFAULT_MIDI_TYPE, jack_port_type_size ()) == 0)
                jack_midi_clear_buffer (buf);
            node = jack_slist_next (node);
            chn++;
        }
    }
    /* reset packet_bufX... */
    packet_bufX = packet_buf + sizeof (jacknet_packet_header) / sizeof (jack_default_audio_sample_t);

    /* ---------- Send ---------- */
    render_jack_ports_to_payload (bitdepth, playback_ports, playback_srcs, nframes, packet_bufX, net_period);

    /* fill in packet hdr */
    pkthdr->transport_state = jack_transport_query (client, &local_trans_pos);
    pkthdr->transport_frame = local_trans_pos.frame;
    pkthdr->framecnt = framecnt;
    pkthdr->latency = latency;
    pkthdr->reply_port = reply_port; 
    pkthdr->sample_rate = jack_get_sample_rate (client);
    pkthdr->period_size = nframes;

    /* playback for us is capture on the other side */
    pkthdr->capture_channels_audio = playback_channels_audio;
    pkthdr->playback_channels_audio = capture_channels_audio;
    pkthdr->capture_channels_midi = playback_channels_midi;
    pkthdr->playback_channels_midi = capture_channels_midi;
    pkthdr->mtu = mtu;
    
    packet_header_hton (pkthdr);
    if (cont_miss < 2*latency+5)
        netjack_sendto (outsockfd, (char *) packet_buf, tx_bufsize, 0, &destaddr, sizeof (destaddr), mtu);
//    else if (cont_miss >= 10 && cont_miss <= 50)
//        printf ("Frame %d  \tToo many packets missed (%d). We have stopped sending data\n", framecnt, cont_miss);
    else if (cont_miss > 50+5*latency)
    {
	state_connected = 0;
        //printf ("Frame %d  \tRealy too many packets missed (%d). Let's reset the counter\n", framecnt, cont_miss);
        cont_miss = 5;
    }

    framecnt++;
    return 0;      
}

/**
 * This is the shutdown callback for this JACK application.
 * It is called by JACK if the server ever shuts down or
 * decides to disconnect the client.
 */

void
jack_shutdown (void *arg)
{
    exit (1);
}

void
init_sockaddr_in (struct sockaddr_in *name , const char *hostname , uint16_t port)
{
    name->sin_family = AF_INET ;
    name->sin_port = htons (port);
    if (hostname)
    {
        struct hostent *hostinfo = gethostbyname (hostname);
        if (hostinfo == NULL)
            fprintf (stderr, "init_sockaddr_in: unknown host: %s.\n", hostname);
        name->sin_addr = *(struct in_addr *) hostinfo->h_addr ;
    }
    else
        name->sin_addr.s_addr = htonl (INADDR_ANY) ;
}

void 
printUsage ()
{
fprintf (stderr, "usage: jack_netsource -h <host peer> [options]\n"
        "\n"
        "  -n <jack name> - Reports a different name to jack\n"
        "  -s <server name> - The name of the local jack server\n"
        "  -h <host_peer> - Host name of the slave JACK\n"
        "  -p <port> - UDP port used by the slave JACK\n"
        "  -P <num channels> - Number of audio playback channels\n"
        "  -C <num channels> - Number of audio capture channels\n"
        "  -o <num channels> - Number of midi playback channels\n"
        "  -i <num channels> - Number of midi capture channels\n"
        "  -l <latency> - Network latency in number of NetJack frames\n"
        "  -r <reply port> - Local UDP port to use\n"
        "  -f <downsample ratio> - Downsample data in the wire by this factor\n"
        "  -b <bitdepth> - Set transport to use 16bit or 8bit\n"
        "  -m <mtu> - Assume this mtu for the link\n"
	"  -c <bytes> - Use Celt and encode <bytes> per channel and packet.\n"
        "\n");
}

int
main (int argc, char *argv[])
{
    /* Some startup related basics */    
    char *client_name, *server_name = NULL, *peer_ip;
    int peer_port = 3000;
    jack_options_t options = JackNullOption;
    jack_status_t status;

    /* Torben's famous state variables, aka "the reporting API" ! */
    int statecopy_connected, statecopy_latency, statecopy_netxruns;

    /* Argument parsing stuff */    
    extern char *optarg;
    extern int optind, optopt;
    int errflg=0, c;

    if (argc < 3)
    {
        printUsage ();
        return 1;
    }
    
    client_name = (char *) malloc (sizeof (char) * 9);
    peer_ip = (char *) malloc (sizeof (char) * 9);
    sprintf(client_name, "netsource");
    sprintf(peer_ip, "localhost");

    while ((c = getopt (argc, argv, ":n:s:h:p:C:P:i:o:l:r:f:b:m:c:")) != -1)
    {
        switch (c)
        {
            case 'n':
            free(client_name);
            client_name = (char *) malloc (sizeof (char) * strlen (optarg));
            strcpy (client_name, optarg);
            break;
            case 's':
            server_name = (char *) malloc (sizeof (char) * strlen (optarg));
            strcpy (server_name, optarg);
            options |= JackServerName;
            break;
            case 'h':
            free(peer_ip);
            peer_ip = (char *) malloc (sizeof (char) * strlen (optarg));
            strcpy (peer_ip, optarg);
            break;
            case 'p':
            peer_port = atoi (optarg);
            break;
            case 'P':
            playback_channels_audio = atoi (optarg);
            break;
            case 'C':
            capture_channels_audio = atoi (optarg);
            break;
            case 'o':
            playback_channels_midi = atoi (optarg);
            break;
            case 'i':
            capture_channels_midi = atoi (optarg);
            break;
            case 'l':
            latency = atoi (optarg);
            break;
            case 'r':
            reply_port = atoi (optarg);
            break;
            case 'f':
            factor = atoi (optarg);
            break;
            case 'b':
            bitdepth = atoi (optarg);
            break;
	    case 'c':
#ifdef HAVE_CELT
	    bitdepth = 1000;
	    factor = atoi (optarg);
#else
	    printf( "not built with celt supprt\n" );
	    exit(10);
#endif
	    break;
            case 'm':
            mtu = atoi (optarg);
            break;
            case ':':
            fprintf (stderr, "Option -%c requires an operand\n", optopt);
            errflg++;
            break;
            case '?':
            fprintf (stderr, "Unrecognized option: -%c\n", optopt);
            errflg++;
        }
    }
    if (errflg)
    {
        printUsage ();
        exit (2);
    }

    capture_channels = capture_channels_audio + capture_channels_midi;
    playback_channels = playback_channels_audio + playback_channels_midi;
    
    outsockfd = socket (PF_INET, SOCK_DGRAM, 0);
    insockfd = socket (PF_INET, SOCK_DGRAM, 0);
    init_sockaddr_in ((struct sockaddr_in *) &destaddr, peer_ip, peer_port);
    if(reply_port)
    {
        init_sockaddr_in ((struct sockaddr_in *) &bindaddr, NULL, reply_port);
        bind (insockfd, &bindaddr, sizeof (bindaddr));
    }

    /* try to become a client of the JACK server */
    client = jack_client_open (client_name, options, &status, server_name);
    if (client == NULL)
    {
        fprintf (stderr, "jack_client_open() failed, status = 0x%2.0x\n"
                         "Is the JACK server running ?\n", status);
        return 1;
    }

    /* Set up jack callbacks */
    jack_set_process_callback (client, process, 0);
    jack_set_sync_callback (client, sync_cb, 0);
    jack_on_shutdown (client, jack_shutdown, 0);

    alloc_ports (capture_channels_audio, playback_channels_audio, capture_channels_midi, playback_channels_midi);

    jack_nframes_t net_period = (float) jack_get_buffer_size (client) / (float) factor;
    int rx_bufsize =  get_sample_size (bitdepth) * capture_channels * net_period + sizeof (jacknet_packet_header);
    global_packcache = packet_cache_new (latency + 5, rx_bufsize, mtu);

    /* tell the JACK server that we are ready to roll */
    if (jack_activate (client))
    {
        fprintf (stderr, "Cannot activate client");
        return 1;
    }

    /* Now sleep forever... and evaluate the state_ vars */

    statecopy_connected = 2; // make it report unconnected on start.
    statecopy_latency = state_latency;
    statecopy_netxruns = state_netxruns;

    while (1)
    {
        sleep (1);
        if (statecopy_connected != state_connected)
        {
            statecopy_connected = state_connected;
            if (statecopy_connected)
            {
                state_netxruns = 1; // We want to reset the netxrun count on each new connection
                printf ("Connected :-)\n");
            }
            else
                printf ("Not Connected\n");
        }

	    if (statecopy_connected)
	    {
            if (statecopy_netxruns != state_netxruns) {
            statecopy_netxruns = state_netxruns;
            printf ("at frame %06d -> total netxruns %d\n", state_currentframe, statecopy_netxruns);
            }
        }
        else
        {
            if (statecopy_latency != state_latency)
            {
                statecopy_latency = state_latency;
                if (statecopy_latency > 1)
                printf ("current latency %d\n", statecopy_latency);
            }
        }
    }

    /* Never reached. Well we will be a GtkApp someday... */
    packet_cache_free (global_packcache);
    jack_client_close (client);
    exit (0);
}
