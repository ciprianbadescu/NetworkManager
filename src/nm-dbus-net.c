/* NetworkManager -- Network link manager
 *
 * Dan Williams <dcbw@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * (C) Copyright 2005 Red Hat, Inc.
 */

#include "nm-device.h"
#include "nm-device-802-11-wireless.h"
#include "NetworkManagerDbus.h"
#include "NetworkManagerAP.h"
#include "NetworkManagerAPList.h"
#include "NetworkManagerUtils.h"
#include "nm-dbus-net.h"
#include "nm-utils.h"
#include "nm-device-private.h"


/*
 * nm_dbus_get_ap_from_object_path
 *
 * Returns the network (ap) associated with a dbus object path
 *
 */
static NMAccessPoint *
nm_dbus_get_ap_from_object_path (const char *path,
                                 NMDevice *dev)
{
	NMAccessPoint *		ap = NULL;
	NMAccessPointList *	ap_list;
	NMAPListIter *		iter;
	char				compare_path[100];
	char *				escaped_compare_path;

	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (dev != NULL, NULL);

	ap_list = nm_device_802_11_wireless_ap_list_get (NM_DEVICE_802_11_WIRELESS (dev));
	if (!ap_list)
		return NULL;

	if (!(iter = nm_ap_list_iter_new (ap_list)))
		return NULL;

	while ((ap = nm_ap_list_iter_next (iter))) {
		int len;

		snprintf (compare_path, 100, "%s/%s/Networks/%s", NM_DBUS_PATH_DEVICE,
				nm_device_get_iface (dev), nm_ap_get_essid (ap));
		escaped_compare_path = nm_dbus_escape_object_path (compare_path);

		len = strlen(escaped_compare_path);
		if (strncmp (path, escaped_compare_path, len) == 0) {
			/* Differentiate between 'foo' and 'foo-a' */
			if (path[len] == '\0' || path[len] == '/') {
				g_free (escaped_compare_path);
				break;
			}
		}
		g_free (escaped_compare_path);
	}
	nm_ap_list_iter_free (iter);

	return ap;
}


static DBusMessage *
nm_dbus_net_get_name (DBusConnection *connection,
                      DBusMessage *message,
                      gpointer user_data)
{
	NMDbusCBData * data = (NMDbusCBData *) user_data;
	DBusMessage	*reply = NULL;
	const char *essid;

	g_return_val_if_fail (connection != NULL, NULL);
	g_return_val_if_fail (message != NULL, NULL);
	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (data->ap != NULL, NULL);

	if (!(reply = dbus_message_new_method_return (message))) {
		nm_warning ("Not enough memory to create dbus message.");
		return NULL;
	}

	essid = nm_ap_get_essid (data->ap);
	dbus_message_append_args (reply, DBUS_TYPE_STRING, &essid, DBUS_TYPE_INVALID);

	return reply;
}

static DBusMessage *
nm_dbus_net_get_address (DBusConnection *connection,
                         DBusMessage *message,
                         gpointer user_data)
{
	NMDbusCBData *	data = (NMDbusCBData *) user_data;
	DBusMessage	*	reply = NULL;
	char			buf[20];

	g_return_val_if_fail (connection != NULL, NULL);
	g_return_val_if_fail (message != NULL, NULL);
	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (data->ap != NULL, NULL);

	if (!(reply = dbus_message_new_method_return (message))) {
		nm_warning ("Not enough memory to create dbus message.");
		return NULL;
	}

	memset (&buf[0], 0, 20);
	iw_ether_ntop((const struct ether_addr *) (nm_ap_get_address (data->ap)), &buf[0]);
	dbus_message_append_args (reply, DBUS_TYPE_STRING, &buf, DBUS_TYPE_INVALID);

	return reply;
}

static DBusMessage *
nm_dbus_net_get_strength (DBusConnection *connection,
                          DBusMessage *message,
                          gpointer user_data)
{
	NMDbusCBData *		data = (NMDbusCBData *) user_data;
	DBusMessage	*		reply = NULL;
	NMAccessPoint *		tmp_ap = NULL;
	NMAccessPointList *	ap_list;
	NMAPListIter *		iter;
	int					best_strength;

	g_return_val_if_fail (connection != NULL, NULL);
	g_return_val_if_fail (message != NULL, NULL);
	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (data->ap != NULL, NULL);

	if (!(reply = dbus_message_new_method_return (message))) {
		nm_warning ("Not enough memory to create dbus message.");
		return NULL;
	}

	/* We iterate over the device list and return the best strength for all
	 * APs with the given ESSID.
	 */
	best_strength = nm_ap_get_strength (data->ap);
	if (!(ap_list = nm_device_802_11_wireless_ap_list_get (NM_DEVICE_802_11_WIRELESS (data->dev))))
		goto append;

	if (!(iter = nm_ap_list_iter_new (ap_list)))
		goto append;

	/* Find best strength # among all APs that share this essid */
	while ((tmp_ap = nm_ap_list_iter_next (iter))) {
		if (nm_null_safe_strcmp (nm_ap_get_essid (data->ap), nm_ap_get_essid (tmp_ap)) == 0)
			if (nm_ap_get_strength (tmp_ap) > best_strength)
				best_strength = nm_ap_get_strength (tmp_ap);
	}
	nm_ap_list_iter_free (iter);

append:
	dbus_message_append_args (reply, DBUS_TYPE_INT32, &best_strength, DBUS_TYPE_INVALID);

	return reply;
}

static DBusMessage *
nm_dbus_net_get_frequency (DBusConnection *connection,
                           DBusMessage *message,
                           gpointer user_data)
{
	NMDbusCBData *	data = (NMDbusCBData *) user_data;
	DBusMessage	*	reply = NULL;
	double			freq;

	g_return_val_if_fail (connection != NULL, NULL);
	g_return_val_if_fail (message != NULL, NULL);
	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (data->ap != NULL, NULL);

	if (!(reply = dbus_message_new_method_return (message))) {
		nm_warning ("Not enough memory to create dbus message.");
		return NULL;
	}

	freq = nm_ap_get_freq (data->ap);
	dbus_message_append_args (reply, DBUS_TYPE_DOUBLE, &freq, DBUS_TYPE_INVALID);

	return reply;
}

static DBusMessage *
nm_dbus_net_get_rate (DBusConnection *connection,
                      DBusMessage *message,
                      gpointer user_data)
{
	NMDbusCBData *	data = (NMDbusCBData *) user_data;
	DBusMessage	*	reply = NULL;
	dbus_int32_t	rate;

	g_return_val_if_fail (connection != NULL, NULL);
	g_return_val_if_fail (message != NULL, NULL);
	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (data->ap != NULL, NULL);

	if (!(reply = dbus_message_new_method_return (message))) {
		nm_warning ("Not enough memory to create dbus message.");
		return NULL;
	}

	rate = nm_ap_get_rate (data->ap);
	dbus_message_append_args (reply, DBUS_TYPE_INT32, &rate, DBUS_TYPE_INVALID);

	return reply;
}

static DBusMessage *
nm_dbus_net_get_encrypted (DBusConnection *connection,
                           DBusMessage *message,
                           gpointer user_data)
{
	NMDbusCBData *	data = (NMDbusCBData *) user_data;
	DBusMessage	*	reply = NULL;
	dbus_bool_t		is_encrypted;

	g_return_val_if_fail (connection != NULL, NULL);
	g_return_val_if_fail (message != NULL, NULL);
	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (data->ap != NULL, NULL);

	if (!(reply = dbus_message_new_method_return (message))) {
		nm_warning ("Not enough memory to create dbus message.");
		return NULL;
	}

	is_encrypted = nm_ap_get_encrypted (data->ap);
	dbus_message_append_args (reply, DBUS_TYPE_BOOLEAN, &is_encrypted, DBUS_TYPE_INVALID);

	return reply;
}

static DBusMessage *
nm_dbus_net_get_mode (DBusConnection *connection,
                      DBusMessage *message,
                      gpointer user_data)
{
	NMDbusCBData *	data = (NMDbusCBData *) user_data;
	DBusMessage	*	reply = NULL;
	dbus_int32_t 	mode;

	g_return_val_if_fail (connection != NULL, NULL);
	g_return_val_if_fail (message != NULL, NULL);
	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (data->ap != NULL, NULL);

	if (!(reply = dbus_message_new_method_return (message))) {
		nm_warning ("Not enough memory to create dbus message.");
		return NULL;
	}

	mode = (dbus_int32_t) nm_ap_get_mode (data->ap);
	dbus_message_append_args (reply, DBUS_TYPE_INT32, &mode, DBUS_TYPE_INVALID);

	return reply;
}


static DBusMessage *
nm_dbus_net_get_properties (DBusConnection *connection,
                            DBusMessage *message,
                            gpointer user_data)
{
	NMDbusCBData *	data = (NMDbusCBData *) user_data;
	DBusMessage	*	reply = NULL;
	char *			op;
	const char *	essid;
	char			hw_addr_buf[20];
	char *			hw_addr_buf_ptr = &hw_addr_buf[0];
	dbus_int32_t	strength;
	double 			freq;
	dbus_int32_t	rate;
	dbus_int32_t	mode;
	dbus_int32_t	capabilities;
	dbus_bool_t		broadcast;

	g_return_val_if_fail (connection != NULL, NULL);
	g_return_val_if_fail (message != NULL, NULL);
	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (data->ap != NULL, NULL);
	g_return_val_if_fail (data->dev != NULL, NULL);

	if (!(reply = dbus_message_new_method_return (message))) {
		nm_warning ("Not enough memory to create dbus message.");
		return NULL;
	}

	op = nm_dbus_get_object_path_for_network (data->dev, data->ap);
	essid = nm_ap_get_essid (data->ap);
	strength = nm_ap_get_strength (data->ap);
	freq = nm_ap_get_freq (data->ap);
	rate = nm_ap_get_rate (data->ap);
	mode = (dbus_int32_t) nm_ap_get_mode (data->ap);
	capabilities = (dbus_int32_t) nm_ap_get_capabilities (data->ap);
	broadcast = (dbus_bool_t) nm_ap_get_broadcast (data->ap);

	memset (&hw_addr_buf[0], 0, 20);
	if (nm_ap_get_address (data->ap))
		iw_ether_ntop((const struct ether_addr *) (nm_ap_get_address (data->ap)), &hw_addr_buf[0]);

	dbus_message_append_args (reply,
	                          DBUS_TYPE_OBJECT_PATH, &op,
	                          DBUS_TYPE_STRING,  &essid,
	                          DBUS_TYPE_STRING,  &hw_addr_buf_ptr,
	                          DBUS_TYPE_INT32,   &strength,
	                          DBUS_TYPE_DOUBLE,  &freq,
	                          DBUS_TYPE_INT32,   &rate,
	                          DBUS_TYPE_INT32,   &mode,
	                          DBUS_TYPE_INT32,   &capabilities,
	                          DBUS_TYPE_BOOLEAN, &broadcast,
	                          DBUS_TYPE_INVALID);
	g_free (op);

	return reply;
}


gboolean
nm_dbus_net_methods_dispatch (NMDbusMethodList *list,
                              DBusConnection *connection,
                              DBusMessage *message,
                              gpointer user_data,
                              DBusMessage **reply)
{
	NMDbusCBData *	cb_data = (NMDbusCBData *) user_data;
	const char *	path;
	NMAccessPoint *	ap;
	gboolean		handled = FALSE;

	g_return_val_if_fail (list != NULL, FALSE);
	g_return_val_if_fail (connection != NULL, FALSE);
	g_return_val_if_fail (message != NULL, FALSE);
	g_return_val_if_fail (cb_data != NULL, FALSE);
	g_return_val_if_fail (cb_data->dev != NULL, FALSE);

	/* Figure out which network the request is for */
	path = dbus_message_get_path (message);
	if ((ap = nm_dbus_get_ap_from_object_path (path, cb_data->dev))) {
		cb_data->ap = ap;
		handled = nm_dbus_method_list_dispatch (list,
		                                        connection,
		                                        message,
		                                        cb_data,
		                                        reply);
	} else {
		*reply = nm_dbus_create_error_message (message,
		                                       NM_DBUS_INTERFACE,
		                                       "NetworkNotFound",
		                                       "The requested network does not "
		                                       "exist for this device.");
		handled = TRUE;
	}

	return handled;
}

/*
 * nm_dbus_net_methods_setup
 *
 * Register handlers for dbus methods on the
 * org.freedesktop.NetworkManager.Devices.<dev>.Networks object.
 *
 */
NMDbusMethodList *nm_dbus_net_methods_setup (NMData *data)
{
	NMDbusMethodList * list;

	g_return_val_if_fail (data != NULL, NULL);

	list = nm_dbus_method_list_new ("", FALSE, data, NULL);

 	nm_dbus_method_list_add_method (list, "getProperties",	nm_dbus_net_get_properties);
	nm_dbus_method_list_add_method (list, "getName",		nm_dbus_net_get_name);
	nm_dbus_method_list_add_method (list, "getAddress",		nm_dbus_net_get_address);
	nm_dbus_method_list_add_method (list, "getStrength",	nm_dbus_net_get_strength);
	nm_dbus_method_list_add_method (list, "getFrequency",	nm_dbus_net_get_frequency);
	nm_dbus_method_list_add_method (list, "getRate",		nm_dbus_net_get_rate);
	nm_dbus_method_list_add_method (list, "getEncrypted",	nm_dbus_net_get_encrypted);
	nm_dbus_method_list_add_method (list, "getMode",		nm_dbus_net_get_mode);

	return (list);
}
