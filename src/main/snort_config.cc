/*
** Copyright (C) 2014 Cisco and/or its affiliates. All rights reserved.
** Copyright (C) 2013-2013 Sourcefire, Inc.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License Version 2 as
** published by the Free Software Foundation.  You may not use, modify or
** distribute this program under any other version of the GNU General
** Public License.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include "snort_config.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <thread>

#include "snort_types.h"
#include "detection/treenodes.h"
#include "events/event_queue.h"
#include "stream5/stream_api.h"
#include "port_scan/ps_detect.h"  // FIXIT for PS_PROTO_*
#include "stream5/stream_tcp.h"
#include "stream5/stream_udp.h"
#include "utils/strvec.h"
#include "file_api/file_service.h"
#include "target_based/sftarget_reader.h"
#include "side_channel/sidechannel.h"
#include "parser/parser.h"
#include "parser/config_file.h"
#include "parser/vars.h"
#include "filters/rate_filter.h"
#include "managers/mpse_manager.h"
#include "managers/inspector_manager.h"

#define DEFAULT_PAF_MAX 16384

//-------------------------------------------------------------------------
// private implementation
//-------------------------------------------------------------------------

#ifdef SIDE_CHANNEL
static void FreeSideChannelModuleConfigs(SideChannelModuleConfig *head)
{
    while (head != NULL)
    {
        SideChannelModuleConfig *tmp = head;

        head = head->next;

        if (tmp->keyword != NULL)
            free(tmp->keyword);

        if (tmp->opts != NULL)
            free(tmp->opts);

        if (tmp->file_name != NULL)
            free(tmp->file_name);

        free(tmp);
    }
}
#endif

static void FreeRuleStateList(RuleState *head)
{
    while (head != NULL)
    {
        RuleState *tmp = head;

        head = head->next;

        free(tmp);
    }
}

static void FreeClassifications(ClassType *head)
{
    while (head != NULL)
    {
        ClassType *tmp = head;

        head = head->next;

        if (tmp->name != NULL)
            free(tmp->name);

        if (tmp->type != NULL)
            free(tmp->type);

        free(tmp);
    }
}

static void FreeReferences(ReferenceSystemNode *head)
{
    while (head != NULL)
    {
        ReferenceSystemNode *tmp = head;

        head = head->next;

        if (tmp->name != NULL)
            free(tmp->name);

        if (tmp->url != NULL)
            free(tmp->url);

        free(tmp);
    }
}

typedef struct _IgnoredRuleList
{
    OptTreeNode *otn;
    struct _IgnoredRuleList *next;
} IgnoredRuleList;

#if 0
/** Get rule list for a specific protocol
 *
 * @param rule
 * @param ptocool protocol type
 * @returns RuleTreeNode* rule list for specific protocol
 */
static inline RuleTreeNode * protocolRuleList(RuleListNode *rule, int protocol)
{
    switch (protocol)
    {
        case IPPROTO_TCP:
            return rule->RuleList->TcpList;
        case IPPROTO_UDP:
            return rule->RuleList->UdpList;
        case IPPROTO_ICMP:
            break;
        default:
            break;
    }
    return NULL;
}
#endif
static inline const char* getProtocolName (int protocol)
{
    static const char *protocolName[] = {"TCP", "UDP", "ICMP"};
    switch (protocol)
    {
        case IPPROTO_TCP:
            return protocolName[0];
        case IPPROTO_UDP:
            return protocolName[1];
        case IPPROTO_ICMP:
            return protocolName[2];
            break;
        default:
            break;
    }
    return NULL;
}

/**check whether a flow bit is set for an option node.
 *
 * @param otn Option Tree Node
 * @returns 0 - no flow bit is set, 1 otherwise
 */
#if 0
static int OtnHasFlowOrFlowbit(OptTreeNode *otn)
{
    if (otn_has_plugin(otn, RULE_OPTION_TYPE_FLOW) ||
        otn_has_plugin(otn, RULE_OPTION_TYPE_FLOWBIT))
    {
        return 1;
    }
    return 0;
}

static void addRuleToIgnoreList(IgnoredRuleList **ppIgnoredRuleList, OptTreeNode *otn)
{
    IgnoredRuleList *ignored_rule;

    ignored_rule = (IgnoredRuleList*)SnortAlloc(sizeof(*ignored_rule));
    ignored_rule->otn = otn;
    ignored_rule->next = *ppIgnoredRuleList;
    *ppIgnoredRuleList = ignored_rule;
}

static void printIgnoredRules(
    IgnoredRuleList *pIgnoredRuleList,
    int any_any_flow
    )
{
    char six_sids = 0;
    int sids_ignored = 0;
    char buf[STD_BUF];
    IgnoredRuleList *ignored_rule;
    IgnoredRuleList *next_ignored_rule;

    buf[0] = '\0';

    for (ignored_rule = pIgnoredRuleList; ignored_rule != NULL; )
    {
        if (any_any_flow == 0)
        {
            if (six_sids == 1)
            {
                SnortSnprintfAppend(buf, STD_BUF-1, "\n");
                LogMessage("%s", buf);
                six_sids = 0;
            }

            if (sids_ignored == 0)
            {
                SnortSnprintf(buf, STD_BUF-1, "    %d:%d",
                        ignored_rule->otn->sigInfo.generator,
                        ignored_rule->otn->sigInfo.id);
            }
            else
            {
                SnortSnprintfAppend(buf, STD_BUF-1, ", %d:%d",
                        ignored_rule->otn->sigInfo.generator,
                        ignored_rule->otn->sigInfo.id);
            }
            sids_ignored++;
            if (sids_ignored %6 == 0)
            {
                /* Have it print next time through */
                six_sids = 1;
                sids_ignored = 0;
            }
        }
        next_ignored_rule = ignored_rule->next;
        free(ignored_rule);
        ignored_rule = next_ignored_rule;
    }

    if (sids_ignored || six_sids)
    {
        SnortSnprintfAppend(buf, STD_BUF-1, "\n");
        LogMessage("%s", buf);
    }
}

/**Determines whether any_any_flow should be ignored or not.
 *
 * Dont ignore any_any_flows if flow bit is set on an any_any_flow,
 * or ignoreAnyAnyRules is not set.
 * @param portList port list
 * @param rtn Rule tree node
 * @param any_any_flow - set if any_any_flow is ignored,0 otherwise
 * @param ppIgnoredRuleList
 * @param ignoreAnyAnyRules
 * @returns
 */
static int AnyAnyFlow(
    uint16_t *portList,
    OptTreeNode *otn,
    RuleTreeNode*,
    int any_any_flow,
    IgnoredRuleList **ppIgnoredRuleList,
    int ignoreAnyAnyRules)
{
    /**if any_any_flow is set then following code has no effect.*/
    if (any_any_flow)
    {
        return any_any_flow;
    }

    /* Look for an OTN with flow or flowbits keyword */
    if (OtnHasFlowOrFlowbit(otn))
    {
        int i;

        for (i=1;i<=MAX_PORTS;i++)
        {
            /* track sessions for ALL ports becuase
             * of any -> any with flow/flowbits */
            portList[i] |= PORT_MONITOR_SESSION;
        }
        return 1;
    }

    if (ignoreAnyAnyRules)
    {

        /* if not, then ignore the content/pcre/etc */
        if (otn_has_plugin(otn, RULE_OPTION_TYPE_CONTENT) ||
                otn_has_plugin(otn, RULE_OPTION_TYPE_CONTENT_URI) ||
                otn_has_plugin(otn, RULE_OPTION_TYPE_BYTE_TEST) ||
                otn_has_plugin(otn, RULE_OPTION_TYPE_PCRE))
        {
            /* Ignoring this rule.... */
            addRuleToIgnoreList(ppIgnoredRuleList, otn);
        }
    }

    return 0;
}

/**initialize given port list from the given ruleset, for a given policy
 * @param portList pointer to array of MAX_PORTS+1 uint8_t. This array content
 * is changed by walking through the rulesets.
 * @param protocol - protocol type
 */
static void setPortFilterList(
        SnortConfig* sc,uint16_t *portList,
        int protocol,
        int ignoreAnyAnyRules,
        PolicyId policyId
        )
{
    char *port_array = NULL;
    int num_ports = 0;
    int i;
    RuleTreeNode *rtn;
    OptTreeNode *otn;
    int inspectSrc, inspectDst;
    char any_any_flow = 0;
    IgnoredRuleList *pIgnoredRuleList = NULL;     ///list of ignored rules
    SFGHASH_NODE *hashNode;
    int flowBitIsSet = 0;

    if ((protocol == IPPROTO_TCP) && (ignoreAnyAnyRules == 0))
    {
        int j;
        for (j=0; j<MAX_PORTS; j++)
        {
            portList[j] |= PORT_MONITOR_SESSION | PORT_MONITOR_INSPECT;
        }
        return;
    }

    /* Post-process TCP rules to establish TCP ports to inspect. */
    for (hashNode = sfghash_findfirst(sc->otn_map);
         hashNode;
         hashNode = sfghash_findnext(sc->otn_map))
    {
        otn = (OptTreeNode *)hashNode->data;
        flowBitIsSet = OtnHasFlowOrFlowbit(otn);

        rtn = getRtnFromOtn(otn, policyId);

        if (!rtn)
        {
            continue;
        }

        if (rtn->proto == protocol)
        {
            //do operation
            inspectSrc = inspectDst = 0;
            if (PortObjectHasAny(rtn->src_portobject))
            {
                inspectSrc = -1;
            }
            else
            {
                port_array = PortObjectCharPortArray(port_array, rtn->src_portobject, &num_ports);
                if (port_array && num_ports != 0)
                {
                    inspectSrc = 1;
                    for (i=0;i<SFPO_MAX_PORTS;i++)
                    {
                        if (port_array[i])
                        {
                            portList[i] |= PORT_MONITOR_INSPECT;
                            /* port specific rule */
                                /* Look for an OTN with flow or flowbits keyword */
                                if (flowBitIsSet)
                                {
                                    portList[i] |= PORT_MONITOR_SESSION;
                                }
                        }
                    }
                }
                if ( port_array )
                {
                    free(port_array);
                    port_array = NULL;
                }
            }
            if (PortObjectHasAny(rtn->dst_portobject))
            {
                inspectDst = -1;
            }
            else
            {
                port_array = PortObjectCharPortArray(port_array, rtn->dst_portobject, &num_ports);
                if (port_array && num_ports != 0)
                {
                    inspectDst = 1;
                    for (i=0;i<SFPO_MAX_PORTS;i++)
                    {
                        if (port_array[i])
                        {
                            portList[i] |= PORT_MONITOR_INSPECT;
                            /* port specific rule */
                                if (flowBitIsSet)
                                {
                                    portList[i] |= PORT_MONITOR_SESSION;
                                }
                        }
                    }
                }
                if ( port_array )
                {
                    free(port_array);
                    port_array = NULL;
                }
            }
            if ((inspectSrc == -1) && (inspectDst == -1))
            {
                /* any -> any rule */
                if (any_any_flow == 0)
                {
                    any_any_flow = AnyAnyFlow(portList, otn, rtn, any_any_flow,
                            &pIgnoredRuleList, ignoreAnyAnyRules);
                }
            }
        }
    }

    // If portscan is tracking TCP/UDP, need to create sessions for all ports 
    if (((protocol == IPPROTO_UDP) && (ScGetScannedProtocols(sc) & PS_PROTO_UDP))
     || ((protocol == IPPROTO_TCP) && (ScGetScannedProtocols(sc) & PS_PROTO_TCP)))
    {
        int j;
        for (j=0; j<MAX_PORTS; j++)
        {
            portList[j] |= PORT_MONITOR_SESSION;
        }
    }

    if (any_any_flow == 1)
    {
        const char* protocolName = getProtocolName(protocol);

        LogMessage("WARNING: 'ignore_any_rules' option for Stream5 %s "
            "disabled because of %s rule with flow or flowbits option.\n",
            protocolName, protocolName);
    }

    else if (pIgnoredRuleList)
    {
        const char* protocolName = getProtocolName(protocol);

        LogMessage("WARNING: Rules (GID:SID) effectively ignored because of "
            "'ignore_any_rules' option for Stream5 %s.\n", protocolName);
    }
    // free list; print iff any_any_flow
    printIgnoredRules(pIgnoredRuleList, any_any_flow);

}
#endif

//-------------------------------------------------------------------------
// public methods
//-------------------------------------------------------------------------

/* Alot of this initialization can be skipped if not running in IDS mode
 * but the goal is to minimize config checks at run time when running in
 * IDS mode so we keep things simple and enforce that the only difference
 * among run_modes is how we handle packets via the log_func. */
SnortConfig * SnortConfNew(void)
{
    SnortConfig *sc = (SnortConfig *)SnortAlloc(sizeof(SnortConfig));

    sc->pkt_cnt = 0;
    sc->pkt_skip = 0;
    sc->pkt_snaplen = -1;
    sc->output_flags = 0;

    /*user_id and group_id should be initialized to -1 by default, because
     * chown() use this later, -1 means no change to user_id/group_id*/
    sc->user_id = -1;
    sc->group_id = -1;

    sc->tagged_packet_limit = 256;
    sc->default_rule_state = RULE_STATE_ENABLED;

    // FIXIT pcre_match_limit* are interdependent
    // somehow a packet thread needs a much lower setting 
    sc->pcre_match_limit = 1500;
    sc->pcre_match_limit_recursion = 1500;

    memset(sc->pid_path, 0, sizeof(sc->pid_path));
    memset(sc->pid_filename, 0, sizeof(sc->pid_filename));
    memset(sc->pidfile_suffix, 0, sizeof(sc->pidfile_suffix));

    /* Default max size of the attribute table */
    sc->max_attribute_hosts = DEFAULT_MAX_ATTRIBUTE_HOSTS;
    sc->max_attribute_services_per_host = DEFAULT_MAX_ATTRIBUTE_SERVICES_PER_HOST;

    /* Default max number of services per rule */
    sc->max_metadata_services = DEFAULT_MAX_METADATA_SERVICES;
    sc->mpls_stack_depth = DEFAULT_LABELCHAIN_LENGTH;

    sc->max_threads = 1;
    InspectorManager::new_config(sc);

    sc->var_list = NULL;

    sc->state = (SnortState*)SnortAlloc(
        sizeof(SnortState)*sc->max_threads);

    sc->policy_map = new PolicyMap();

    set_inspection_policy(sc->get_inspection_policy());
    set_ips_policy(sc->get_ips_policy());
    set_network_policy(sc->get_network_policy());

    return sc;
}

void SnortConfFree(SnortConfig *sc)
{
    if (sc == NULL)
        return;

    if (sc->log_dir != NULL)
        free(sc->log_dir);

    if (sc->orig_log_dir != NULL)
        free(sc->orig_log_dir);

    if (sc->bpf_file != NULL)
        free(sc->bpf_file);

    if (sc->pcap_log_file != NULL)
        free(sc->pcap_log_file);

    if (sc->chroot_dir != NULL)
        free(sc->chroot_dir);

    if (sc->alert_file != NULL)
        free(sc->alert_file);

    if (sc->perf_file != NULL)
        free(sc->perf_file);

    if (sc->bpf_filter != NULL)
        free(sc->bpf_filter);

    if (sc->event_trace_file != NULL)
        free(sc->event_trace_file);

#ifdef PERF_PROFILING
    if (sc->profile_rules.filename != NULL)
        free(sc->profile_rules.filename);

    if (sc->profile_preprocs.filename != NULL)
        free(sc->profile_preprocs.filename);
#endif

#ifdef SIDE_CHANNEL
    FreeSideChannelModuleConfigs(sc->side_channel_config.module_configs);
#endif

    if (sc->base_version != NULL)
        free(sc->base_version);

    FreeRuleStateList(sc->rule_state_list);
    FreeClassifications(sc->classifications);
    FreeReferences(sc->references);

    FreeRuleLists(sc);
    OtnLookupFree(sc->otn_map);
    PortTablesFree(sc->port_tables);

    ThresholdConfigFree(sc->threshold_config);
    RateFilter_ConfigFree(sc->rate_filter_config);
    DetectionFilterConfigFree(sc->detection_filter_config);

    if ( sc->event_queue_config )
        EventQueueConfigFree(sc->event_queue_config);

    if (sc->ip_proto_only_lists != NULL)
    {
        unsigned int j;

        for (j = 0; j < NUM_IP_PROTOS; j++)
            sflist_free_all(sc->ip_proto_only_lists[j], NULL);

        free(sc->ip_proto_only_lists);
    }

    fpDeleteFastPacketDetection(sc);

    InspectorManager::delete_config(sc);

    if ( sc->react_page )
        free(sc->react_page);

    if ( sc->daq_type )
        free(sc->daq_type);

    if ( sc->daq_mode )
        free(sc->daq_mode);

    if ( sc->daq_vars )
        StringVector_Delete(sc->daq_vars);

    if ( sc->daq_dirs )
        StringVector_Delete(sc->daq_dirs);

    if ( sc->respond_device )
        free(sc->respond_device);

     if (sc->eth_dst )
        free(sc->eth_dst);

    if (sc->gtp_ports)
        free(sc->gtp_ports);

#ifdef REG_TEST

    if(sc->ha_out)
        free(sc->ha_out);

    if(sc->ha_in)
        free(sc->ha_in);
#endif

    free_file_config(sc->file_config);

#ifdef SIDE_CHANNEL
    if (sc->side_channel_config.opts)
        free(sc->side_channel_config.opts);
#endif

    if ( sc->var_list )
        FreeVarList(sc->var_list);

    if ( !snort_conf || sc == snort_conf ||
         (sc->fast_pattern_config &&
         (sc->fast_pattern_config->search_api !=
             snort_conf->fast_pattern_config->search_api)) )
    {
        MpseManager::stop_search_engine(sc->fast_pattern_config->search_api);
    }
    FastPatternConfigFree(sc->fast_pattern_config);

    delete sc->policy_map;

    free(sc->state);
    free(sc);
}

SnortConfig * MergeSnortConfs(SnortConfig *cmd_line, SnortConfig *config_file)
{
    /* Move everything from the command line config over to the
     * config_file config */

    if (cmd_line == NULL)
    {
        FatalError("%s(%d) Merging snort configs: snort conf is NULL.\n",
                   __FILE__, __LINE__);
    }

    if (config_file == NULL)
    {
        if (cmd_line->log_dir == NULL)
            cmd_line->log_dir = SnortStrdup(DEFAULT_LOG_DIR);
    }
    else if ((cmd_line->log_dir == NULL) && (config_file->log_dir == NULL))
    {
        config_file->log_dir = SnortStrdup(DEFAULT_LOG_DIR);
    }
    else if (cmd_line->log_dir != NULL)
    {
        if (config_file->log_dir != NULL)
            free(config_file->log_dir);

        config_file->log_dir = SnortStrdup(cmd_line->log_dir);
    }

    if (config_file == NULL)
        return cmd_line;

    /* Used because of a potential chroot */
    config_file->orig_log_dir = SnortStrdup(config_file->log_dir);

    config_file->event_log_id = cmd_line->event_log_id;

    config_file->run_flags |= cmd_line->run_flags;
    config_file->output_flags |= cmd_line->output_flags;
    config_file->logging_flags |= cmd_line->logging_flags;

    if ((cmd_line->run_flags & RUN_FLAG__TEST) &&
        (config_file->run_flags & RUN_FLAG__DAEMON))
    {
        /* Just ignore deamon setting in conf file */
        config_file->run_flags &= ~RUN_FLAG__DAEMON;
    }

    // only set by cmd_line to override other conf output settings
    config_file->output = cmd_line->output;

    /* Merge checksum flags.  If command line modified them, use from the
     * command line, else just use from config_file. */

    int cl_chk = cmd_line->get_network_policy()->checksum_eval;
    int cl_drop = cmd_line->get_network_policy()->checksum_drop;

    for ( auto p : config_file->policy_map->network_policy )
    {
        if ( !(cl_chk & CHECKSUM_FLAG__DEF) )
            p->checksum_eval = cl_chk;

        if ( !(cl_drop & CHECKSUM_FLAG__DEF) )
            p->checksum_eval = cl_drop;
    }

    if (cmd_line->pid_path[0] != '\0')
        ConfigPidPath(config_file, cmd_line->pid_path);

    if (cmd_line->obfuscation_net.family != 0)
        memcpy(&config_file->obfuscation_net, &cmd_line->obfuscation_net, sizeof(sfip_t));

    if (cmd_line->homenet.family != 0)
        memcpy(&config_file->homenet, &cmd_line->homenet, sizeof(sfip_t));

    if (cmd_line->bpf_file != NULL)
    {
        if (config_file->bpf_file != NULL)
            free(config_file->bpf_file);
        config_file->bpf_file = SnortStrdup(cmd_line->bpf_file);
    }

    if (cmd_line->bpf_filter != NULL)
        config_file->bpf_filter = SnortStrdup(cmd_line->bpf_filter);

    if (cmd_line->pkt_snaplen != -1)
        config_file->pkt_snaplen = cmd_line->pkt_snaplen;

    if (cmd_line->pkt_cnt != 0)
        config_file->pkt_cnt = cmd_line->pkt_cnt;

    if (cmd_line->pkt_skip != 0)
        config_file->pkt_skip = cmd_line->pkt_skip;

    if (cmd_line->group_id != -1)
        config_file->group_id = cmd_line->group_id;

    if (cmd_line->user_id != -1)
        config_file->user_id = cmd_line->user_id;

    /* Only configurable on command line */
    if (cmd_line->pcap_log_file != NULL)
        config_file->pcap_log_file = SnortStrdup(cmd_line->pcap_log_file);

    if (cmd_line->file_mask != 0)
        config_file->file_mask = cmd_line->file_mask;

    if (cmd_line->pidfile_suffix[0] != '\0')
    {
        SnortStrncpy(config_file->pidfile_suffix, cmd_line->pidfile_suffix,
                     sizeof(config_file->pidfile_suffix));
    }

    if (cmd_line->chroot_dir != NULL)
    {
        if (config_file->chroot_dir != NULL)
            free(config_file->chroot_dir);
        config_file->chroot_dir = SnortStrdup(cmd_line->chroot_dir);
    }

    if (cmd_line->perf_file != NULL)
    {
        if (config_file->perf_file != NULL)
            free(config_file->perf_file);
        config_file->perf_file = SnortStrdup(cmd_line->perf_file);
    }

    if ( cmd_line->daq_type )
        config_file->daq_type = SnortStrdup(cmd_line->daq_type);

    if ( cmd_line->daq_mode )
        config_file->daq_mode = SnortStrdup(cmd_line->daq_mode);

    if ( cmd_line->dirty_pig )
        config_file->dirty_pig = cmd_line->dirty_pig;

    if ( cmd_line->daq_vars )
    {
        /* Command line overwrites daq_vars */
        if (config_file->daq_vars)
            StringVector_Delete(config_file->daq_vars);

        config_file->daq_vars = StringVector_New();
        StringVector_AddVector(config_file->daq_vars, cmd_line->daq_vars);
    }
    if ( cmd_line->daq_dirs )
    {
        /* Command line overwrites daq_dirs */
        if (config_file->daq_dirs)
            StringVector_Delete(config_file->daq_dirs);

        config_file->daq_dirs = StringVector_New();
        StringVector_AddVector(config_file->daq_dirs, cmd_line->daq_dirs);
    }
    if (cmd_line->mpls_stack_depth != DEFAULT_LABELCHAIN_LENGTH)
        config_file->mpls_stack_depth = cmd_line->mpls_stack_depth;

    /* Set MPLS payload type here if it hasn't been defined */
    if ((cmd_line->mpls_payload_type == 0) &&
        (config_file->mpls_payload_type == 0))
    {
        config_file->mpls_payload_type = DEFAULT_MPLS_PAYLOADTYPE;
    }
    else if (cmd_line->mpls_payload_type != 0)
    {
        config_file->mpls_payload_type = cmd_line->mpls_payload_type;
    }

    if (cmd_line->run_flags & RUN_FLAG__PROCESS_ALL_EVENTS)
        config_file->event_queue_config->process_all_events = 1;

#ifdef REG_TEST
    config_file->ha_peer = cmd_line->ha_peer;

    if ( cmd_line->ha_out )
    {
        if(config_file->ha_out != NULL)
            free(config_file->ha_out);
        config_file->ha_out = strdup(cmd_line->ha_out);
    }

    if ( cmd_line->ha_in )
    {
        if(config_file->ha_in != NULL)
            free(config_file->ha_in);
        config_file->ha_in = strdup(cmd_line->ha_in);
    }
#endif

    if ( cmd_line->max_threads )
        config_file->max_threads = cmd_line->max_threads;

    if ( config_file->max_threads <= 0 )
        config_file->max_threads = std::thread::hardware_concurrency();

    if ( cmd_line->remote_control )
        config_file->remote_control = cmd_line->remote_control;

    // config file vars are stored differently
    // FIXIT should config_file and cmd_line use the same var list / table?
    config_file->var_list = NULL;

    free(config_file->state);
    config_file->state = (SnortState*)SnortAlloc(
        sizeof(SnortState)*config_file->max_threads);

    return config_file;
}

int VerifyReload(SnortConfig *sc)
{
    if (sc == NULL)
        return -1;

    if ((snort_conf->alert_file != NULL) && (sc->alert_file != NULL))
    {
        if (strcasecmp(snort_conf->alert_file, sc->alert_file) != 0)
        {
            ErrorMessage("Snort Reload: Changing the alert file "
                         "configuration requires a restart.\n");
            return -1;
        }
    }
    else if (snort_conf->alert_file != sc->alert_file)
    {
        ErrorMessage("Snort Reload: Changing the alert file "
                     "configuration requires a restart.\n");
        return -1;
    }

    if (snort_conf->asn1_mem != sc->asn1_mem)
    {
        ErrorMessage("Snort Reload: Changing the asn1 memory configuration "
                     "requires a restart.\n");
        return -1;
    }

    if ((sc->bpf_filter == NULL) && (sc->bpf_file != NULL))
        sc->bpf_filter = read_infile(sc->bpf_file);

    if ((sc->bpf_filter != NULL) && (snort_conf->bpf_filter != NULL))
    {
        if (strcasecmp(snort_conf->bpf_filter, sc->bpf_filter) != 0)
        {
            ErrorMessage("Snort Reload: Changing the bpf filter configuration "
                         "requires a restart.\n");
            return -1;
        }
    }
    else if (sc->bpf_filter != snort_conf->bpf_filter)
    {
        ErrorMessage("Snort Reload: Changing the bpf filter configuration "
                     "requires a restart.\n");
        return -1;
    }

    if ( sc->respond_attempts != snort_conf->respond_attempts ||
         sc->respond_device != snort_conf->respond_device )
    {
        ErrorMessage("Snort Reload: Changing config response "
                     "requires a restart.\n");
        return -1;
    }

    if ((snort_conf->chroot_dir != NULL) &&
        (sc->chroot_dir != NULL))
    {
        if (strcasecmp(snort_conf->chroot_dir, sc->chroot_dir) != 0)
        {
            ErrorMessage("Snort Reload: Changing the chroot directory "
                         "configuration requires a restart.\n");
            return -1;
        }
    }
    else if (snort_conf->chroot_dir != sc->chroot_dir)
    {
        ErrorMessage("Snort Reload: Changing the chroot directory "
                     "configuration requires a restart.\n");
        return -1;
    }

    if ((snort_conf->run_flags & RUN_FLAG__DAEMON) !=
        (sc->run_flags & RUN_FLAG__DAEMON))
    {
        ErrorMessage("Snort Reload: Changing to or from daemon mode "
                     "requires a restart.\n");
        return -1;
    }

    /* Orig log dir because a chroot might have changed it */
    if ((snort_conf->orig_log_dir != NULL) &&
        (sc->orig_log_dir != NULL))
    {
        if (strcasecmp(snort_conf->orig_log_dir, sc->orig_log_dir) != 0)
        {
            ErrorMessage("Snort Reload: Changing the log directory "
                         "configuration requires a restart.\n");
            return -1;
        }
    }
    else if (snort_conf->orig_log_dir != sc->orig_log_dir)
    {
        ErrorMessage("Snort Reload: Changing the log directory "
                     "configuration requires a restart.\n");
        return -1;
    }

    if (snort_conf->max_attribute_hosts != sc->max_attribute_hosts)
    {
        ErrorMessage("Snort Reload: Changing max_attribute_hosts "
                     "configuration requires a restart.\n");
        return -1;
    }
    if (snort_conf->max_attribute_services_per_host != sc->max_attribute_services_per_host)
    {
        ErrorMessage("Snort Reload: Changing max_attribute_services_per_host "
                     "configuration requires a restart.\n");
        return -1;
    }

    if ( (snort_conf->output_flags & OUTPUT_FLAG__NO_LOG) != 
         (sc->output_flags & OUTPUT_FLAG__NO_LOG) )
    {
        ErrorMessage("Snort Reload: Changing from log to no log or vice "
                     "versa requires a restart.\n");
        return -1;
    }

    if ((snort_conf->run_flags & RUN_FLAG__NO_PROMISCUOUS) !=
        (sc->run_flags & RUN_FLAG__NO_PROMISCUOUS))
    {
        ErrorMessage("Snort Reload: Changing to or from promiscuous mode "
                     "requires a restart.\n");
        return -1;
    }

#ifdef PPM_MGR
    /* XXX XXX Not really sure we need to disallow this */
    if (snort_conf->ppm_cfg.rule_log != sc->ppm_cfg.rule_log)
    {
        ErrorMessage("Snort Reload: Changing the ppm rule_log "
                     "configuration requires a restart.\n");
        return -1;
    }
#endif

#ifdef PERF_PROFILING
    if ((snort_conf->profile_rules.num != sc->profile_rules.num) ||
        (snort_conf->profile_rules.sort != sc->profile_rules.sort) ||
        (snort_conf->profile_rules.append != sc->profile_rules.append))
    {
        ErrorMessage("Snort Reload: Changing rule profiling number, sort "
                     "or append configuration requires a restart.\n");
        return -1;
    }

    if ((snort_conf->profile_rules.filename != NULL) &&
        (sc->profile_rules.filename != NULL))
    {
        if (strcasecmp(snort_conf->profile_rules.filename,
                       sc->profile_rules.filename) != 0)
        {
            ErrorMessage("Snort Reload: Changing the rule profiling filename "
                         "configuration requires a restart.\n");
            return -1;
        }
    }
    else if (snort_conf->profile_rules.filename !=
             sc->profile_rules.filename)
    {
        ErrorMessage("Snort Reload: Changing the rule profiling filename "
                     "configuration requires a restart.\n");
        return -1;
    }

    if ((snort_conf->profile_preprocs.num !=  sc->profile_preprocs.num) ||
        (snort_conf->profile_preprocs.sort != sc->profile_preprocs.sort) ||
        (snort_conf->profile_preprocs.append != sc->profile_preprocs.append))
    {
        ErrorMessage("Snort Reload: Changing preprocessor profiling number, "
                     "sort or append configuration requires a restart.\n");
        return -1;
    }

    if ((snort_conf->profile_preprocs.filename != NULL) &&
        (sc->profile_preprocs.filename != NULL))
    {
        if (strcasecmp(snort_conf->profile_preprocs.filename,
                       sc->profile_preprocs.filename) != 0)
        {
            ErrorMessage("Snort Reload: Changing the preprocessor profiling "
                         "filename configuration requires a restart.\n");
            return -1;
        }
    }
    else if (snort_conf->profile_preprocs.filename !=
             sc->profile_preprocs.filename)
    {
        ErrorMessage("Snort Reload: Changing the preprocessor profiling "
                     "filename configuration requires a restart.\n");
        return -1;
    }
#endif

    if (snort_conf->group_id != sc->group_id)
    {
        ErrorMessage("Snort Reload: Changing the group id "
                     "configuration requires a restart.\n");
        return -1;
    }

    if (snort_conf->user_id != sc->user_id)
    {
        ErrorMessage("Snort Reload: Changing the user id "
                     "configuration requires a restart.\n");
        return -1;
    }

    if (snort_conf->pkt_snaplen != sc->pkt_snaplen)
    {
        ErrorMessage("Snort Reload: Changing the packet snaplen "
                     "configuration requires a restart.\n");
        return -1;
    }

    if (snort_conf->threshold_config->memcap !=
        sc->threshold_config->memcap)
    {
        ErrorMessage("Snort Reload: Changing the threshold memcap "
                     "configuration requires a restart.\n");
        return -1;
    }

    if (snort_conf->rate_filter_config->memcap !=
        sc->rate_filter_config->memcap)
    {
        ErrorMessage("Snort Reload: Changing the rate filter memcap "
                     "configuration requires a restart.\n");
        return -1;
    }

    if (snort_conf->detection_filter_config->memcap !=
        sc->detection_filter_config->memcap)
    {
        ErrorMessage("Snort Reload: Changing the detection filter memcap "
                     "configuration requires a restart.\n");
        return -1;
    }

    if (snort_conf->so_rule_memcap != sc->so_rule_memcap)
    {
        ErrorMessage("Snort Reload: Changing the so rule memcap "
                     "configuration requires a restart.\n");
        return -1;
    }

#ifdef SIDE_CHANNEL
    if (SideChannelVerifyConfig(sc) != 0)
    {
        ErrorMessage("Snort Reload: Changing the side channel configuration requires a restart.\n");
        return -1;
    }
#endif

    return 0;
}

void SetPortFilterLists(SnortConfig*)
{
#if 0
    // FIXIT this doesn't look correct; in any case it depends on bindings
    // because port filter lists are being pulled from s5
    // iterate over all rules and call into s5 to update port filter list
    for (unsigned pid = 0; pid < sc->num_policies_allocated; pid++)
    {
        SnortPolicy* sp = sc->targeted_policies[pid];

        if ( !sp )
            continue;

        setCurrentPolicy(sc, pid);
        int ignore_any;
        uint16_t* portList;

        /* Post-process TCP rules to establish TCP ports to inspect. */
        if ( sp->s5_config )
            portList = Stream5GetTcpPortList(sp->s5_config, ignore_any);
        else
            portList = NULL;
    
        if ( portList )
            setPortFilterList(sc, portList, IPPROTO_TCP, ignore_any, pid);
    
        /* Post-process UDP rules to establish UDP ports to inspect. */
        if ( sp->s5_config )
            portList = Stream5GetUdpPortList(sp->s5_config, ignore_any);
        else
            portList = NULL;

        if ( portList )
            setPortFilterList(sc, portList, IPPROTO_UDP, ignore_any, pid);
    }
#endif
}

void InitServiceFilterStatus(SnortConfig* sc)
{
    SFGHASH_NODE *hashNode;
    PolicyId policyId = 0;

    if (sc == NULL)
    {
        FatalError("%s(%d) Snort config for parsing is NULL.\n",
                   __FILE__, __LINE__);
    }

    for (hashNode = sfghash_findfirst(sc->otn_map);
         hashNode;
         hashNode = sfghash_findnext(sc->otn_map))
    {
        OptTreeNode *otn = (OptTreeNode *)hashNode->data;

        for ( policyId = 0;
              policyId < otn->proto_node_num;
              policyId++)
        {
            RuleTreeNode *rtn = getRtnFromOtn(otn, policyId);

            if (rtn && (rtn->proto == IPPROTO_TCP))
            {
                unsigned int svc_idx;
                // FIXIT setCurrentPolicy(sc, policyId);

                for (svc_idx = 0; svc_idx < otn->sigInfo.num_services; svc_idx++)
                {
                    if (otn->sigInfo.services[svc_idx].service_ordinal)
                    {
                        stream.set_service_filter_status(
                            sc, otn->sigInfo.services[svc_idx].service_ordinal,
                            PORT_MONITOR_SESSION);
                    }
                }
            }
        }
    }
}
