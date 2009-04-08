#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <string.h>

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include <telepathy-glib/connection-manager.h>
#include <telepathy-glib/connection.h>
#include <telepathy-glib/channel.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/util.h>
#include <telepathy-glib/enums.h>
#include <telepathy-glib/debug.h>

#define UNIX_PATH_MAX    108

static GMainLoop *loop = NULL;
static TpDBusDaemon *bus_daemon = NULL;
static TpConnection *conn = NULL;

struct ft_state
{
	GIOChannel *channel;
	guint64 offset;

	struct sockaddr_un sa;
};

static void
handle_error (const GError *error)
{
	if (error)
	{
		g_print ("ERROR: %s\n", error->message);
		tp_cli_connection_call_disconnect (conn, -1, NULL,
				NULL, NULL, NULL);
	}
}

static void
file_transfer_unix_cb (TpChannel	*channel,
                       const GValue	*addressv,
		       const GError	*in_error,
		       gpointer		 user_data,
		       GObject		*weak_obj)
{
	struct ft_state *state = (struct ft_state *) user_data;

	handle_error (in_error);

	const char *address = g_value_get_string (addressv);
	strncpy (state->sa.sun_path, address, UNIX_PATH_MAX);

	g_print (" > file_transfer_unix_cb (%s)\n", address);
}

static gboolean
file_transfer_data_received (GIOChannel 	*channel,
                             GIOCondition	 condition,
			     gpointer		 data)
{
	char buf[1024];
	gsize bytes_read;
	GError *error = NULL;

	if (condition & G_IO_HUP) return FALSE; /* this channel is done */

	g_io_channel_read_chars (channel, buf, sizeof (buf), &bytes_read,
			&error);
	handle_error (error);

	buf[bytes_read] = '\0';

	g_print ("%s", buf);

	return TRUE;
}

static void
file_transfer_unix_state_changed_cb (TpChannel	*channel,
                                     guint	 state,
				     guint	 reason,
				     gpointer	 user_data,
				     GObject	*weak_obj)
{
	struct ft_state *ftstate = (struct ft_state *) user_data;
	GError *error = NULL;

	g_print (" :: file_transfer_state_changed_cb (%i)\n", state);

	if (state == TP_FILE_TRANSFER_STATE_OPEN)
	{
		int sock = socket (ftstate->sa.sun_family, SOCK_STREAM, 0);
		if (sock == -1)
		{
			int e = errno;
			g_error ("UNABLE TO GET SOCKET: %s", strerror (e));
		}

		if (connect (sock, (struct sockaddr *) &ftstate->sa,
				   sizeof (struct sockaddr_un)))
		{
			int e = errno;
			g_error ("UNABLE TO CONNECT: %s", strerror (e));
		}

		/* turn the socket into an IOChannel, so that we can work
		 * with our main loop */
		ftstate->channel = g_io_channel_unix_new (sock);
		g_io_add_watch (ftstate->channel, G_IO_IN | G_IO_HUP,
				file_transfer_data_received,
				ftstate);
	}
	else if (state == TP_FILE_TRANSFER_STATE_COMPLETED ||
		 state == TP_FILE_TRANSFER_STATE_CANCELLED)
	{
		g_print ("\n--------------EOF--------------\n");
		/* close the socket */
		g_io_channel_shutdown (ftstate->channel, TRUE, &error);
		handle_error (error);
		/* release our resources */
		g_io_channel_unref (ftstate->channel);
		g_slice_free (struct ft_state, ftstate);
		tp_cli_channel_call_close (channel, -1, NULL, NULL, NULL, NULL);
	}
}

static void
file_transfer_channel_ready (TpChannel		*channel,
                             const GError	*in_error,
			     gpointer		 user_data)
{
	GError *error = NULL;

	handle_error (in_error);

	GHashTable *map = tp_channel_borrow_immutable_properties (channel);

	const char *filename = tp_asv_get_string (map,
			TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".Filename");
	guint64 size = tp_asv_get_uint64 (map,
			TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".Size", NULL);

	g_print ("New file transfer to %s -- `%s' (%llu bytes)\n",
			tp_channel_get_identifier (channel),
			filename, size);

	/* File transfers in Telepathy work by opening a socket to the
	 * Connection Manager and streaming the file over that socket.
	 * Let's find out what manner of sockets are supported by this CM */
	GHashTable *sockets = tp_asv_get_boxed (map,
		TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".AvailableSocketTypes",
		TP_HASH_TYPE_SUPPORTED_SOCKET_MAP);

	/* let's try for IPv4 */
	if (g_hash_table_lookup (sockets,
				GINT_TO_POINTER (TP_SOCKET_ADDRESS_TYPE_IPV4)))
	{
		g_print ("ipv4 supported\n");
	}
	else if (g_hash_table_lookup (sockets,
				GINT_TO_POINTER (TP_SOCKET_ADDRESS_TYPE_UNIX)))
	{
		struct ft_state *state = g_slice_new (struct ft_state);
		state->sa.sun_family = AF_UNIX;

		tp_cli_channel_type_file_transfer_connect_to_file_transfer_state_changed (
				channel, file_transfer_unix_state_changed_cb,
				state, NULL, NULL, &error);
		handle_error (error);

		GValue *value = tp_g_value_slice_new_static_string ("");


		tp_g_value_slice_free (value);
	}
}

static void
create_ft_channel_cb (TpConnection	*conn,
                      const char	*object_path,
		      GHashTable	*properties,
		      const GError	*in_error,
		      gpointer		 user_data,
		      GObject		*weak_obj)
{
	GError *error = NULL;
	handle_error (in_error);

	TpChannel *channel = tp_channel_new_from_properties (conn, object_path,
			properties, &error);
	handle_error (error);

	tp_channel_call_when_ready (channel, file_transfer_channel_ready, NULL);
}

static void
conn_ready (TpConnection	*conn,
            const GError	*in_error,
	    gpointer		 user_data)
{
	char **argv = (char **) user_data;
	GError *error = NULL;

	g_print (" > conn_ready\n");

	handle_error (in_error);

	/* check if the Requests interface is available */
	if (tp_proxy_has_interface_by_id (conn,
		TP_IFACE_QUARK_CONNECTION_INTERFACE_REQUESTS))
	{
		/* FIXME: we should check that our client has the
		 * FT capability */

		GFile *file = g_file_new_for_commandline_arg (argv[3]);
		GFileInfo *info = g_file_query_info (file,
				"standard::*",
				G_FILE_QUERY_INFO_NONE,
				NULL, &error);
		handle_error (error);

		GHashTable *props = tp_asv_new (
			TP_IFACE_CHANNEL ".ChannelType", G_TYPE_STRING, TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER,
			TP_IFACE_CHANNEL ".TargetHandleType", G_TYPE_UINT, TP_HANDLE_TYPE_CONTACT,
			TP_IFACE_CHANNEL ".TargetID", G_TYPE_STRING, argv[4],
			TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".Filename", G_TYPE_STRING, g_file_info_get_display_name (info),
			TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".ContentType", G_TYPE_STRING, g_file_info_get_content_type (info),
			TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER ".Size", G_TYPE_UINT64, g_file_info_get_size (info),
			NULL);

		tp_cli_connection_interface_requests_call_create_channel (
				conn, -1, props,
				create_ft_channel_cb,
				NULL, NULL, NULL);

		g_hash_table_destroy (props);
		g_object_unref (info);
		g_object_unref (file);
	}
}

static void
status_changed_cb (TpConnection	*conn,
                   guint	 status,
		   guint	 reason,
		   gpointer	 user_data,
		   GObject	*weak_object)
{
	if (status == TP_CONNECTION_STATUS_DISCONNECTED)
	{
		g_print ("Disconnected\n");
		g_main_loop_quit (loop);
	}
	else if (status == TP_CONNECTION_STATUS_CONNECTED)
	{
		g_print ("Connected\n");
	}
}

static void
request_connection_cb (TpConnectionManager	*cm,
                       const char		*bus_name,
		       const char		*object_path,
		       const GError		*in_error,
		       gpointer			 user_data,
		       GObject			*weak_object)
{
	char **argv = (char **) user_data;
	GError *error = NULL;

	if (in_error) g_error ("%s", in_error->message);

	conn = tp_connection_new (bus_daemon, bus_name, object_path, &error);
	if (error) g_error ("%s", error->message);

	tp_connection_call_when_ready (conn, conn_ready, argv);

	tp_cli_connection_connect_to_status_changed (conn, status_changed_cb,
			NULL, NULL, NULL, &error);
	handle_error (error);

	/* initiate the connection */
	tp_cli_connection_call_connect (conn, -1, NULL, NULL, NULL, NULL);
}

static void
cm_ready (TpConnectionManager	*cm,
	  const GError		*in_error,
	  gpointer		 user_data,
	  GObject		*weak_obj)
{
	char **argv = (char **) user_data;

	g_print (" > cm_ready\n");

	if (in_error) g_error ("%s", in_error->message);

	const TpConnectionManagerProtocol *prot = tp_connection_manager_get_protocol (cm, "local-xmpp");
	if (!prot) g_error ("Protocol is not supported");

	/* request a new connection */
	GHashTable *parameters = tp_asv_new (
			"first-name", G_TYPE_STRING, argv[1],
			"last-name", G_TYPE_STRING, argv[2],
			NULL);

	tp_cli_connection_manager_call_request_connection (cm, -1,
			"local-xmpp",
			parameters,
			request_connection_cb,
			argv, NULL, NULL);

	g_hash_table_destroy (parameters);
}

static void
interrupt_cb (int signal)
{
	g_print ("Interrupt\n");
	/* disconnect */
	tp_cli_connection_call_disconnect (conn, -1, NULL, NULL, NULL, NULL);
}

int
main (int argc, char **argv)
{
	GError *error = NULL;

	g_type_init ();

	if (argc != 5)
	{
		g_error ("Must provide first name, last name, filename and targetID!");
	}

	/* create a main loop */
	loop = g_main_loop_new (NULL, FALSE);

	/* acquire a connection to the D-Bus daemon */
	bus_daemon = tp_dbus_daemon_dup (&error);
	if (bus_daemon == NULL)
	{
		g_error ("%s", error->message);
	}

	/* we want to request the salut CM */
	TpConnectionManager *cm = tp_connection_manager_new (bus_daemon,
			"salut", NULL, &error);
	if (error) g_error ("%s", error->message);

	tp_connection_manager_call_when_ready (cm, cm_ready,
			argv, NULL, NULL);

	/* set up a signal handler */
	struct sigaction sa = { 0 };
	sa.sa_handler = interrupt_cb;
	sigaction (SIGINT, &sa, NULL);

	g_main_loop_run (loop);

	g_object_unref (bus_daemon);

	return 0;
}
