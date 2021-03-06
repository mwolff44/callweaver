/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster at digium.com>
 *
 * See http://www.callweaver.org for more information about
 * the CallWeaver project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/* \file This file generates Doxygen pages from files in the /doc
 directory of the CallWeaver source code tree 
 */

/* The following is for Doxygen Developer's documentation generated
 * by running "make progdocs" with doxygen installed on your
 * system.
 */
/*! \page DevDoc CallWeaver Developer's Documentation - appendices
 *  \arg \ref CodeGuide 
 *  \arg \ref CWAPI
 *  \arg \ref CWDebug
 *  \arg \ref CWAMI
 *  \arg \ref CWARA
 *  \arg \ref CWDUNDi
 *  \arg \ref CWCDR
 *  \arg \ref CWREADME
 *  \arg \ref CWCREDITS
 *  \arg \ref CWVar
 *  \arg \ref CWENUM
 *  \arg \ref ConfigFiles
 *  \arg \ref SoundFiles
 * \section wwwsites Web sites
 * \arg Main:  CallWeaver website http://www.callweaver.org
 * \arg Bugs: The Issue tracker http://trac.callweaver.org
 * \arg Lists: List server http://lists.callweaver.org
 * \arg Wiki: The CallWeaver Wiki 	http://wiki.callweaver.org
 * \arg Docs: The CallWeaver Documentation Project http://www.callweaverdocs.org
 */

/*! \page CodeGuide Coding Guidelines
 *  \section Coding Guidelines
 *  This file is in the /doc directory in your CallWeaver source tree.
 *  Make sure to stay up to date with the latest guidelines.
 *  \verbinclude CODING-GUIDELINES
 */

/*! \page CWAPI CallWeaver API
 *  \section CallWeaverapi CallWeaver API
 *  This programmer's documentation covers the generic API.
 *  \subsection generic Generic Model
 *  \verbinclude model.txt
 *  \subsection channel Channels
 *  \verbinclude channel.txt
 */

/*! \page CWDebug Debugging
 *  \section debug Debugging
 *  \verbinclude README.backtrace
 */

/*! \page CWAMI AMI - The Manager Interface
 *  \section ami AMI - The manager Interface
 *  \arg \link Config_ami Configuration file \endlink
 * \arg \ref manager.c
 *  \verbinclude manager.txt
 */

/*!  \page CWARA ARA - The CallWeaver Realtime Interface
 *  \section realtime ARA - a generic API to storage and retrieval
 *  Implemented in \ref config.c 
 *  Implemented in \ref pbx_realtime.c 
 *  \verbinclude README.realtime 
 *  \verbinclude README.extconfig
 */

/*!  \page CWDUNDi DUNDi
DUNDi is a peer-to-peer system for locating Internet gateways to telephony services. Unlike traditional centralized services (such as the remarkably simple and concise ENUM standard), DUNDi is fully-distributed with no centralized authority whatsoever.

DUNDi is not itself a Voice-over IP signaling or media protocol. Instead, it publishes routes which are in turn accessed via industry standard protocols such as IAX, SIP and H.323. 

 	\arg Dundi is documented at http://www.dundi.com
  	\arg Implemented in \ref pbx_dundi.c and \ref dundi-parser.c
 	\arg Configuration in \link Config_dun dundi.conf \endlink
 */

/*! \page CWCDR CDR - Call Data Records and billing
 * \section cdr Call Data Records
 *  \verbinclude README.cdr
 * \arg \ref cdr.c
 * \arg \ref Config_cdr CDR configuration files
 */
/*! \page CWREADME README - the general administrator introduction
 *  \verbinclude README
 */
 
/*! \page CWCREDITS CREDITS
 *  \verbinclude CREDITS
 */

/*! \page CWVar Global channel variables
 * \section globchan Global Channel Variables
 *  \verbinclude README.variables
 */

/*! \page CWENUM ENUM
 * \section enumreadme ENUM
 * \arg Configuration: \ref Config_enum
 * \arg \ref enum.c
 * \arg \ref app_enumlookup.c
 *
 * \verbinclude README.enum
 */

/*! \page ConfigFiles Configuration files
 * \section config Main configuration files
 * \arg \link Config_ast callweaver.conf - the main configuration file \endlink
 * \arg \link Config_ext extensions.conf - The Dial Plan \endlink
 * \arg \link Config_mod modules.conf - which modules to load and not to load \endlink
 * \arg \link Config_fea features.conf - call features (transfer, parking, etc) \endlink
 * \section chanconf Channel configurations
 * \arg \link Config_iax IAX2 configuration  \endlink
 * \arg \link Config_sip SIP configuration  \endlink
 * \arg \link Config_mgcp MGCP configuration  \endlink
 * \arg \link Config_rtp RTP configuration  \endlink
 * \arg \link Config_DAHDI DAHDI configuration  \endlink
 * \arg \link Config_oss OSS (sound card) configuration  \endlink
 * \arg \link Config_alsa ALSA (sound card) configuration  \endlink
 * \arg \link Config_agent Agent (proxy channel) configuration  \endlink
 * \section appconf Application configuration files
 * \arg \link Config_mm Meetme (conference bridge) configuration  \endlink
 * \arg \link Config_qu Queue system configuration  \endlink
 * \arg \link Config_vm Voicemail configuration  \endlink
 * \section miscconf Miscellenaous configuration files
 * \arg \link Config_adsi Adsi configuration  \endlink
 * \arg \link Config_ami AMI - Manager configuration  \endlink
 * \arg \link Config_ara Realtime configuration  \endlink
 * \arg \link Config_codec Codec configuration  \endlink
 * \arg \link Config_dun Dundi configuration  \endlink
 * \arg \link Config_enum ENUM configuration  \endlink
 * \arg \link Config_moh Music on Hold configuration  \endlink
 * \arg \link Config_vm Voicemail configuration  \endlink
 */

/*! \page Config_ast CallWeaver.conf
 * \verbinclude README.callweaver.conf
 */
/*! \page Config_mod Modules configuration
 * \verbinclude modules.conf.sample
 */

/*! \page Config_fea Call features configuration
 * \section featconf features.conf
 * \verbinclude features.conf.sample
 */

/*! \page Config_ext Extensions.conf - the Dial Plan
 * \section dialplan Extensions.conf 
 * \verbinclude extensions.conf.sample
 */

/*! \page Config_iax IAX2 configuration
 * IAX2 is implemented in \ref chan_iax2.c . 
 * \arg \link iaxreadme README file \endlink
 * \arg \link iaxconfig iax.conf Configuration file example \endlink
 * \section iaxreadme IAX readme file
 * \verbinclude README.iax
 * \section iaxconfig IAX Configuration example
 * \verbinclude iax.conf.sample
 * \section iaxjitter IAX Jitterbuffer information
 * \verbinclude README.jitterbuffer
 */

/*! \page Config_sip SIP configuration
 * Also see \ref Config_rtp RTP configuration
 * \ref chan_sip.c
 * \section sipconf sip.conf
 * \verbinclude sip.conf.sample
 */

/*! \page Config_mgcp MGCP configuration
 * Also see \ref Config_rtp RTP configuration
 * \ref chan_mgcp.c
 * \section mgcpconf mgcp.conf
 * \verbinclude mgcp.conf.sample
 */


/*! \page Config_vm VoiceMail configuration
 * \section vmconf voicemail.conf
 * \ref app_voicemail.c
 * \verbinclude voicemail.conf.sample
 */

/*! \page Config_DAHDI DAHDI configuration
 * \section dahdiconf chan_dahdi.conf
 * \ref chan_dahdi.c
 * \verbinclude chan_dahdi.conf.sample
 */

/*! \page Config_oss OSS configuration
 * \section ossconf oss.conf
 * \ref chan_oss.c
 * \verbinclude oss.conf.sample
 */

/*! \page Config_alsa ALSA configuration
 * \section alsaconf alsa.conf
 * \ref chan_alsa.c
 * \verbinclude alsa.conf.sample
 */

/*! \page Config_agent Agent configuration
 * \section agentconf agents.conf
 * The agent channel is a proxy channel for queues
 * \ref chan_agent.c
 * \verbinclude agents.conf.sample
 */

/*! \page Config_rtp RTP configuration
 * \ref rtp.c
 * Used in \ref chan_sip.c and \ref chan_mgcp.c (and various H.323 channels)
 * \section rtpconf rtp.conf
 * \verbinclude rtp.conf.sample
 */

/*! \page Config_dun Dundi Configuration
 * \arg See also \ref CWDundi
 * \section dundiconf dundi.conf
 * \verbinclude dundi.conf.sample
 */

/*! \page Config_enum ENUM Configuration
 * \section enumconf enum.conf
 * \arg See also \ref enumreadme
 * \arg \ref app_enumlookup.c
 * \verbinclude enum.conf.sample
 */

/*! \page Config_cdr CDR configuration
 * \arg \link cdrconf Main CDR Configuration \endlink
 * \arg \link cdrcustom Custom CDR driver configuration \endlink
 * \arg \link cdrami Manager CDR driver configuration \endlink
 * \arg \link cdrodbc ODBC CDR driver configuration \endlink
 * \arg \link cdrpgsql Postgres CDR driver configuration \endlink
 * \arg \link cdrtds FreeTDS CDR driver configuration (Microsoft SQL Server) \endlink
 * \section cdrconf Main CDR configuration
 * \verbinclude cdr.conf.sample
 * \section cdrcustom Custom CDR driver configuration
 * \arg \ref cdr_custom.c
 * \verbinclude cdr_custom.conf.sample
 * \section cdrami Manager CDR driver configuration
 * See also:
 * \arg \ref CWAMI
 * \arg \ref cdr_manager.c
 * \verbinclude cdr_manager.conf.sample
 * \section cdrodbc ODBC CDR driver configuration
 * See also:
 * \arg http://www.unixodbc.org
 * \arg \ref cdr_odbc.c
 * \verbinclude cdr_odbc.conf.sample
 * \section cdrpgsql Postgres CDR driver configuration
 * \arg \ref cdr_pgsql.c
 * \arg http://www.postgresql.org
 * \verbinclude cdr_pgsql.conf.sample
 * \section cdrtds FreeTDS CDR driver configuration
 * \arg http://www.freetds.org
 * \verbinclude cdr_tds.conf.sample
 */


/*! \page Config_moh Music on Hold Configuration
 * \arg Implemented in \ref res_musiconhold.c
 * \section mohconf musiconhold.conf
 * \verbinclude musiconhold.conf.sample
 */

/*! \page Config_adsi ADSI Configuration
 * \section adsiconf adsi.conf
 * \verbinclude adsi.conf.sample
 */

/*! \page Config_codec CODEC Configuration
 * \section codecsconf codecs.conf
 * \verbinclude codecs.conf.sample
 */

/*! \page Config_ara REALTIME Configuration
 * \arg See also: \CWARA
 * \section extconf extconfig.conf
 * \verbinclude extconfig.conf.sample
 */

/*! \page Config_ami AMI configuration
 * \arg See also: \CWAMI
 * \section amiconf manager.conf
 * \verbinclude manager.conf.sample
 */

/*! \page Config_qu ACD - Queue system configuration
 * \section quconf queues.conf
 * \verbinclude queues.conf.sample
 */

/*! \page Config_mm Meetme - The conference bridge configuration
 * \section mmconf meetme.conf
 * \verbinclude meetme.conf.sample
 */

/*! \page SoundFiles Sound files
 *  \section SecSound CallWeaver Sound files
 *  CallWeaver includes a large amount of sound files. Many of these
 *  are used by applications and demo scripts within callweaver.
 *
 *  \section SoundList List of included sound files
 *  \verbinclude sounds.txt
 */
