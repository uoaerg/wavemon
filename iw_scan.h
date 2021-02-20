/*
 * Scan-related definitions.
 */
#include "iw_if.h"

/*
 *	Organization of scan results
 */
/**
 * struct scan_entry  -  Representation of a single scan result.
 * @ap_addr:	     MAC address
 * @essid:	     station SSID (may be empty)
 * @freq:	     frequency in MHz
 * @chan:	     channel corresponding to @freq (where applicable)
 * @has_key:	     whether using encryption or not
 * @ht_capable:	     whether this is an HT station
 * @rm_enabled:	     whether Radio Measurement is enabled
 * @mesh_enabled:    whether station advertises mesh services
 * @last_seen:	     time station was last seen (in seconds)
 * @tsf:	     value of the Timing Synchronisation Function counter
 * @bss_signal:	     signal strength of BSS probe in dBm (or 0)
 * @bss_signal_qual: unitless signal strength of BSS probe, 0..100
 * @bss_capa:	     BSS capability flags
 * @bss_sta_count:   BSS station count
 * @bss_chan_usage:  BSS channel utilisation
 */
struct scan_entry {
	struct ether_addr	ap_addr;
	char			essid[MAX_ESSID_LEN + 2];
	uint32_t		freq;
	int			chan;
	bool			has_key:1,
				ht_capable:1,
				rm_enabled:1,
				mesh_enabled:1;

	uint32_t		last_seen;
	uint64_t		tsf;

	int8_t			bss_signal;
	uint8_t			bss_signal_qual;
	uint16_t		bss_capa;
	uint8_t			bss_sta_count,
				bss_chan_usage;

	struct scan_entry	*next;
};
extern void sort_scan_list(struct scan_entry **headp);

/**
 * struct cnt - count frequency of integer numbers
 * @val:	value to count
 * @count:	how often @val occurs
 */
struct cnt {
	int	val;
	int	count;
};

/**
 * struct scan_result - Structure to aggregate all collected scan data.
 * @head:	   begin of scan_entry list (may be NULL)
 * @msg:	   error message, if any
 * @max_essid_len: maximum ESSID-string length (up to %MAX_ESSID_LEN)
 * @channel_stats: array of channel statistics entries
 * @num.total:     number of entries in list starting at @head
 * @num.open:      number of open entries among @num.total
 * @num.hidden:    number of entries with hidden ESSIDs among @num.total
 * @num.two_gig:   number of 2.4GHz stations among @num.total
 * @num.five_gig:  number of 5 GHz stations among @num.total
 * @num.ch_stats:  length of @channel_stats array
 * @mutex:         protects against concurrent consumer/producer access
 */
struct scan_result {
	struct scan_entry *head;
	char		  msg[128];
	uint16_t	  max_essid_len;
	struct cnt	  *channel_stats;
	struct assorted_numbers {
		uint16_t	entries,
				open,
				hidden,
				two_gig,
				five_gig;
/* Maximum number of 'top' statistics entries. */
#define MAX_CH_STATS		3
		size_t		ch_stats;
	}		  num;
	pthread_mutex_t   mutex;
};

extern void *do_scan(void *sr_ptr);

/*
 * Information ID elements.
 * References denote 802.11-2012 sections, unless otherwise noted.
 */
typedef enum {
	IE_SSID              =   0, // 8.4.2.2: Identity of ESS or IBSS
	IE_SUPPORTED_RATES   =   1, // 8.4.2.3: Up to 8 supported rates
	IE_FH_PARAM_SET      =   2, // 8.4.2.4: STA FH PHY synchronization parameters
	IE_DS_PARAM_SET      =   3, // 8.4.2.5: STA DSSS PHY synchronization parameters
	IE_CF_PARAM_SET      =   4, // 8.4.2.6: CF parameters for PCF support
	IE_TIM               =   5, // 8.4.2.7: TIM element (DTIM parameters)
	IE_IBSS              =   6, // 8.4.2.8: IBSS parameter set
	IE_COUNTRY           =   7, // 8.4.2.10: Country (PHY regulatory domain)
	IE_FH_PAT_PARAMS     =   8, // 8.4.2.11: Hopping Pattern parameters
	IE_FH_PAT_TABLE      =   9, // 8.4.2.12: Hopping Pattern table
	IE_REQUEST           =  10, // 8.4.2.13: Request element (Probe req/resp)
	IE_BSS_LOAD          =  11, // 8.4.2.30: BSS Load (STA population/traffic)
	IE_EDCA_PARAM_SET    =  12, // 8.4.2.31: EDCA Parameter Set (QoS)
	IE_TSPEC             =  13, // 8.4.2.32: TSPEC (QoS traffic expectations)
	IE_TCLAS             =  14, // 8.4.2.33: TCLAS (identifies TS)
	IE_SCHEDULE          =  15, // 8.4.2.36: Schedule announcement
	IE_CHALLENGE_TXT     =  16, // 8.4.2.9: Challenge text

	/* 17-31 reserved */

	IE_PWR_CONSTRAINT    =  32, // 8.4.2.16: Power constraint (max allowed TX power)
	IE_PWR_CAPABILITY    =  33, // 8.4.2.17: Power capability (min/max TX power)
	IE_TPC_REQUEST       =  34, // 8.4.2.18: TPC request  (TX power / link margin)
	IE_TPC_REPORT        =  35, // 8.4.2.19: TPC response (TX power / link margin)
	IE_SUPP_CHANNELS     =  36, // 8.4.2.20: Supported channels (channel subbands)
	IE_CHANNEL_SW_ANN    =  37, // 8.4.2.21: Channel switch announcement
	IE_MEASURE_REQUEST   =  38, // 8.4.2.23: Measurement request
	IE_MEASURE_REPOR     =  39, // 8.4.2.24: Measurement report
	IE_QUIET             =  40, // 8.4.2.25: Quiet interval
	IE_IBSS_DFS          =  41, // 8.4.2.26: IBSS DFS operation parameters
	IE_ERP_INFO          =  42, // 8.4.2.14: ERP data rate / Barker preambles
	IE_TS_DELAY          =  43, // 8.4.2.34: TS Delay (ADDTS response)
	IE_TCLAS_PROCESSING  =  44, // 8.4.2.35: TCLAS Processing (ADDTS req/resp)
	IE_HT_CAPABILITIES   =  45, // 8.4.2.58: HT capabilities (declares STA to be HT)
	IE_QOS_CAPABILITY    =  46, // 8.4.2.37: QoS capabilities (at a QoS STA)
	/* 47 reserved */
	IE_RSN               =  48, // 8.4.2.27: RSN (authentication / cipher)
	/* 49 reserved */
	IE_EXT_SUPP_RATES    =  50, // 8.4.2.15: Extended Support Rates information
	IE_AP_CHANNEL_REPORT =  51, // 8.4.2.38: AP channel report (likely location of APs)
	IE_NEIGHBOUR_REPORT  =  52, // 8.4.2.39: Neighbour report
	IE_RCPI              =  53, // 8.4.2.40: RCPI (received frame power level)
	IE_MOBILITY_DOMAIN   =  54, // 8.4.2.49: Mobility domain parameters (MDID, FT, ...)
	IE_FAST_BSS_TRANS    =  55, // 8.4.2.50: Fast BSS transition (FTE)
	IE_TIMEOUT_INTERVAL  =  56, // 8.4.2.51: Timeout Interval Element (TIE)
	IE_RIC_DATA_ELEMENT  =  57, // 8.4.2.52: Resource Information (RIC data, RDE)
	IE_DSE_LOCATION      =  58, // 8.4.2.54: DSE registered location (lat/long/alt)
	IE_SUPP_OPER_CLASSES =  59, // 8.4.2.56: Supported (allowed) operating classes
	IE_EXT_CHAN_SWITCH   =  60, // 8.4.2.55: Extended channel switch announcement
	IE_HT_OPERATION      =  61, // 8.4.2.59: HT STA operation element
	IE_SEC_CHAN_OFFSET   =  62, // 8.4.2.22: Secondary channel offset (40Mhz channel)
	IE_BSS_AVG_ACCESS    =  63, // 8.4.2.41: BSS average access delay (BSS load measure)
	IE_ANTENNA           =  64, // 8.4.2.42: Antenna ID element
	IE_RSNI              =  65, // 8.4.2.43: RSNI (received signal-to-noise)
	IE_MEAS_PILOT_TRAN   =  66, // 8.4.2.44: Measure Pilot Transmission
	IE_BSS_ADMN_CAPACITY =  67, // 8.4.2.45: BSS Available Admission Capacity
	IE_BSS_ACCESS_DELAY  =  68, // 8.4.2.46: BSS AC (Access Category) Access Delay field
	IE_TIME_ADVERTISEMT  =  69, // 8.4.2.63: Time advertisement (clock, offset, ...)
	IE_RM_CAPABILITIES   =  70, // 8.4.2.47: Radio measurement capabilities (RM enabled)
	IE_MULTIPLE_BSSID    =  71, // 8.4.2.48: Multiple BSSID indicator
	IE_20_40_COEXISTENCE =  72, // 8.4.2.62: 20/40 STA BSS coexistence in same spectrum
	IE_20_40_INTOL_CHANS =  73, // 8.4.2.60: 20/40 Intolerant channels report
	IE_OBSS_SCAN_PARAMS  =  74, // 8.4.2.61: Overlapping BSS scan (OBSS) parameters
	IE_RIC_DESCRIPTOR    =  75, // 8.4.2.53: RIC descriptor (fast BSS transition)
	IE_MGMT_MIC_ELEM     =  76, // 8.4.2.57: Management MIC element (MME)
	/* 77 unused */
	IE_EVENT_REQUEST     =  78, // 8.4.2.69: Event request
	IE_EVENT_REPORT      =  79, // 8.4.2.70: Event report
	IE_DIAGNOSTIC_REQ    =  80, // 8.4.2.71: Diagnostic request
	IE_DIAGNOSTIC_REP    =  81, // 8.4.2.72: Diagnostic report
	IE_LOCATION_PARAMS   =  82, // 8.4.2.73: Location parameters (for location services)
	IE_NON_TRANSM_BSSID  =  83, // 8.4.2.74: Nontransmitted BSSID Capability
	IE_SSID_LIST         =  84, // 8.4.2.75: SSID List
	IE_MULTI_BSSID_IDX   =  85, // 8.4.2.76: Multiple BSSID Index (with DTIM information)
	IE_FMS_DESCRIPTOR    =  86, // 8.4.2.77: FMS Descriptor (group-addressed BUs)
	IE_FMS_REQUEST       =  87, // 8.4.2.78: FMS Request (group-addressed frames)
	IE_FMS_RESPONSE      =  88, // 8.4.2.79: FMS Response (group-addressed frames)
	IE_QOS_TRAFFIC_CAPA  =  89, // 8.4.2.80: QoS traffic capability information
	IE_BSS_MAX_IDLE_PER  =  90, // 8.4.2.81: Maximum idle period of a non-AP STA
	IE_TFS_REQUEST       =  91, // 8.4.2.82: TFS (traffic filters) request
	IE_TFS_RESPONSE      =  92, // 8.4.2.83: TFS (traffic filters) response
	IE_WNM_SLEEP_MODE    =  93, // 8.4.2.84: WNM Sleep Mode
	IE_TIM_BROADCAST_REQ =  94, // 8.4.2.85: Periodic TIM broadcast information request
	IE_TIM_BROADCAST_RES =  95, // 8.4.2.86: Periodic TIM broadcast information response
	IE_INTERFERENCE_REPT =  96, // 8.4.2.87: Co-located interference report
	IE_CHANNEL_USAGE     =  97, // 8.4.2.88: Channel usage (non-infrastructure / off-channel TDLS)
	IE_TIME_ZONE         =  98, // 8.4.2.89: Local time zone of the AP
	IE_DMS_REQUEST       =  99, // 8.4.2.90: DMS (group addressed frames) request
	IE_DMS_RESPONSE      = 100, // 8.4.2.91: DMS (group addressed frames) response
	IE_LINK_IDENTIFIER   = 101, // 8.4.2.64: Link identifier (identifies TDLS direct link)
	IE_WAKEUP_SCHEDULE   = 102, // 8.4.2.65: Periodic wakeup schedule (TDLS Peer Power Save Mode)
	/* 103 unused */
	IE_CHAN_SWITCH_TIM   = 104, // 8.4.2.66: Channel Switch time and timeout
	IE_PTI_CONTROL       = 105, // 8.4.2.67: PTI control (traffic buffered at TPU buffer STA)
	IE_TPU_BUFFER_STATUS = 106, // 8.4.2.68: TPU buffer status (traffic buffered at TPU buffer STA)
	IE_INTERWORKING      = 107, // 8.4.2.94: Interworking service capabilities of a STA
	IE_ADVERTISEMT_PROTO = 108, // 8.4.2.95: Advertisement protocol and control
	IE_EXPED_BNDWDTH_REQ = 109, // 8.4.2.96: Expedited bandwidth request (ADDTS request frame)
	IE_QOS_MAP_SET       = 110, // 8.4.2.97: QoS map set (from AP to non-AP STA)
	IE_ROAMING_CONSORTM  = 111, // 8.4.2.98: Roaming consortium (SSP)
	IE_EMERGENCY_ALRT_ID = 112, // 8.4.2.99: Emergency alert identifier (availabe EAS messages)
	IE_MESH_CONFIG       = 113, // 8.4.2.100: Mesh configuration (advertises mesh services)
	IE_MESH_ID           = 114, // 8.4.2.101: Mesh ID (advertises the identification of an MBSS)
	IE_MESH_LINK_REPORT  = 115, // 8.4.2.102: Mesh link report (mesh STA to neighbour peer mesh STA)
	IE_MESH_CONGESTION   = 116, // 8.4.2.103: Mesh congestion notification
	IE_MESH_PEERING_MGMT = 117, // 8.4.2.104: Mesh peering management (with a neighbour mesh STA)
	IE_MESH_CHAN_SWITCH  = 118, // 8.4.2.105: Mesh channel switch (change operating channel / class)
	IE_MESH_AWAKE_WINDOW = 119, // 8.4.2.106: Mesh awake window (beacon frames)
	IE_BEACON_TIMING     = 120, // 8.4.2.107: Beacon timing (of neighbour STAs)
	IE_MCCAOP_SETUP_REQ  = 121, // 8.4.2.108: MCCAOP setup request (MCCAOP reservation)
	IE_MCCAOP_SETUP_RES  = 122, // 8.4.2.109: MCCAOP setup response
	IE_MCCAOP_ADVERTMT   = 123, // 8.4.2.111: MCCAOP advertisement overview (by a mesh STA)
	IE_MCCAOP_TEARDOWN   = 124, // 8.4.2.112: MCCAOP teardown (of MCCAOP reservation)
	IE_GATE_ANNOUNCEMENT = 125, // 8.4.2.113: Mesh Gate Announcement (GANN) in the MBSS
	IE_ROOT_ANNOUNCEMENT = 126, // 8.4.2.114: Mesh Root Announcement (RANN) of root mesh STA
	IE_EXT_CAPABILITIES  = 127, // 8.4.2.29: Extended capabilities information
	/* 128-129 reserved */
	IE_MESH_PATH_REQUEST = 130, // 8.4.2.115: Mesh path request (PREQ)
	IE_MESH_PATH_REPLY   = 131, // 8.4.2.116: Mesh path reply   (PREP)
	IE_MESH_PATH_ERROR   = 132, // 8.4.2.117: Mesh path error   (PERR)
	/* 133-136 reserved */
	IE_MESH_PROXY_PXU    = 137, // 8.4.2.118: Mesh proxy update (PXU)
	IE_MESH_PROXY_PXUC   = 138, // 8.4.2.119: Mesh proxy update confirmation (PXUC)
	IE_MESH_PEERING_AUTH = 139, // 8.4.2.120: Authenticated mesh peering exchange
	IE_MESH_MESSAGE_MIC  = 140, // 8.4.2.121: Mesh message integrity code (MIC) (of peering mgmt frames)
	IE_DESTINATION_URI   = 141, // 8.4.2.92: Destination URI (URI/ESS detection interval values)
	IE_U_APSD_COEXIST    = 142, // 8.4.2.93: U-APSD coexistence (duration of requested transmission)

	/* 143-173 reserved */

	IE_MCCAOP_ADV_OVERVW = 174, // 8.4.2.110: MCCAOP advertisement overview (mesh STA, MCCA information)

	/* 175-220 reserved */

	IE_VENDOR_SPECIFIC   = 221, // 8.4.2.28: Vendor specific information (non-standard)

	/* 222-255 reserved */
} ie_id_t;
