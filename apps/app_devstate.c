/*
 * Devstate application
 * 
 * Since we like the snom leds so much, a little app to
 * light the lights on the snom on demand ....
 *
 * Copyright (C) 2005, Druid Software
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "callweaver/lock.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/callweaver_db.h"
#include "callweaver/utils.h"
#include "callweaver/cli.h"
#include "callweaver/manager.h"
#include "callweaver/devicestate.h"


static char type[] = "DS";
static const char tdesc[] = "Application for sending device state messages";

static void *devstate_app;
static const char devstate_name[] = "DevState";
static const char devstate_synopsis[] = "Generate a device state change event given the input parameters";
static const char devstate_syntax[] = "DevState(device, state)";
static const char devstate_descrip[] = "Generate a device state change event given the input parameters. Returns 0. State values match the callweaver device states. They are 0 = unknown, 1 = not inuse, 2 = inuse, 3 = busy, 4 = invalid, 5 = unavailable, 6 = ringing\n";

static char devstate_cli_usage[] = 
"Usage: DevState device state\n" 
"       Generate a device state change event given the input parameters.\n Mainly used for lighting the LEDs on the snoms.\n";

static int devstate_cli(int fd, int argc, char *argv[]);
static struct opbx_clicmd  cli_dev_state = {
	.cmda = { "devstate", NULL },
	.handler = devstate_cli,
	.summary = "Set the device state on one of the \"pseudo devices\".",
	.usage = devstate_cli_usage,
};


static int devstate_cli(int fd, int argc, char *argv[])
{
    if ((argc != 3) && (argc != 4) && (argc != 5))
        return RESULT_SHOWUSAGE;

    if (opbx_db_put("DEVSTATES", argv[1], argv[2]))
    {
        opbx_log(LOG_DEBUG, "opbx_db_put failed\n");
    }
	opbx_device_state_changed("DS/%s", argv[1]);
    
    return RESULT_SUCCESS;
}

static int devstate_exec(struct opbx_channel *chan, int argc, char **argv, char *result, size_t result_max)
{
    struct localuser *u;

    if (argc != 2)
        return opbx_function_syntax(devstate_syntax);

    LOCAL_USER_ADD(u);
    
    if (opbx_db_put("DEVSTATES", argv[0], argv[1])) {
        opbx_log(LOG_DEBUG, "opbx_db_put failed\n");
    }

    opbx_device_state_changed("DS/%s", argv[0]);

    LOCAL_USER_REMOVE(u);
    return 0;
}


static int ds_devicestate(void *data)
{
    char *dest = data;
    char stateStr[16];
    if (opbx_db_get("DEVSTATES", dest, stateStr, sizeof(stateStr)))
    {
        opbx_log(LOG_DEBUG, "ds_devicestate couldnt get state in opbxdb\n");
        return 0;
    }
    else
    {
        opbx_log(LOG_DEBUG, "ds_devicestate dev=%s returning state %d\n",
               dest, atoi(stateStr));
        return (atoi(stateStr));
    }
}

static struct opbx_channel_tech devstate_tech = {
	.type = type,
	.description = tdesc,
	.capabilities = ((OPBX_FORMAT_MAX_AUDIO << 1) - 1),
	.devicestate = ds_devicestate,
	.requester = NULL,
	.send_digit = NULL,
	.send_text = NULL,
	.call = NULL,
	.hangup = NULL,
	.answer = NULL,
	.read = NULL,
	.write = NULL,
	.bridge = NULL,
	.exception = NULL,
	.indicate = NULL,
	.fixup = NULL,
	.setoption = NULL,
};

static char mandescr_devstate[] = 
"Description: Put a value into opbxdb\n"
"Variables: \n"
"	Family: ...\n"
"	Key: ...\n"
"	Value: ...\n";

static int action_devstate(struct mansession *s, struct message *m)
{
        char *devstate = astman_get_header(m, "Devstate");
        char *value = astman_get_header(m, "Value");
	char *id = astman_get_header(m,"ActionID");

	if (!strlen(devstate)) {
		astman_send_error(s, m, "No Devstate specified");
		return 0;
	}
	if (!strlen(value)) {
		astman_send_error(s, m, "No Value specified");
		return 0;
	}

        if (!opbx_db_put("DEVSTATES", devstate, value)) {
	    opbx_device_state_changed("DS/%s", devstate);
	    opbx_cli(s->fd, "Response: Success\r\n");
	} else {
	    opbx_log(LOG_DEBUG, "opbx_db_put failed\n");
	    opbx_cli(s->fd, "Response: Failed\r\n");
	}
	if (id && !opbx_strlen_zero(id))
		opbx_cli(s->fd, "ActionID: %s\r\n",id);
	opbx_cli(s->fd, "\r\n");
	return 0;
}

static int load_module(void)
{
    if (opbx_channel_register(&devstate_tech)) {
        opbx_log(LOG_DEBUG, "Unable to register channel class %s\n", type);
        return -1;
    }
    opbx_cli_register(&cli_dev_state);  
    opbx_manager_register2( "Devstate", EVENT_FLAG_CALL, action_devstate, "Change a device state", mandescr_devstate );
    devstate_app = opbx_register_function(devstate_name, devstate_exec, devstate_synopsis, devstate_syntax, devstate_descrip);
    return 0;
}

static int unload_module(void)
{
    int res = 0;

    opbx_manager_unregister( "Devstate");
    opbx_cli_unregister(&cli_dev_state);
    res |= opbx_unregister_function(devstate_app);
    opbx_channel_unregister(&devstate_tech);    
    return res;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
