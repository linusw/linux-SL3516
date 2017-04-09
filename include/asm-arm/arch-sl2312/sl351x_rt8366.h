/**************************************************************************
*	sl351x_rtl8366.h
*
*	Description:
*		- Define Realtek 8366 switch
*		- Only the commonly implemented MII registers and its bit definition are defined here.
*	
*	History:
*
*   07/171/2008   CH HSU Modify from Cameo Communications, Inc.
*-----------------------------------------------------------------------------*/
#define	MII_CONTROL_REG		0
#define	MII_STATUS_REG		1
#define	MII_PHY_ID0			2
#define	MII_PHY_ID1			3
#define	MII_LOCAL_CAP		4
#define	MII_REMOTE_CAP		5
#define	MII_EXT_AUTONEG		6
#define	MII_LOCAL_NEXT_PAGE	7
#define	MII_REMOTE_NEXT_PAGE	8
#define	MII_GIGA_CONTROL	9
#define	MII_GIGA_STATUS		10
#define	MII_EXT_STATUS_REG	15

// Control register
#define	MII_CONTROL_RESET	15
#define	MII_CONTROL_LOOPBACK	14
#define	MII_CONTROL_100MBPS	13
#define	MII_CONTROL_AUTONEG	12
#define	MII_CONTROL_POWERDOWN	11
#define	MII_CONTROL_ISOLATE	10
#define	MII_CONTROL_RENEG	9
#define	MII_CONTROL_FULLDUPLEX	8
#define	MII_CONTROL_COLL_TEST	7
#define	MII_CONTROL_1000MBPS	6

// Status/Extended status register
#define	MII_STATUS_100_T4	15	// Basic status
#define	MII_STATUS_100_TX_FULL	14
#define	MII_STATUS_100_TX_HALF	13
#define	MII_STATUS_10_TX_FULL	12
#define	MII_STATUS_10_TX_HALF	11
#define	MII_STATUS_100_T2_FULL	10
#define	MII_STATUS_100_T2_HALF	9
#define	MII_STATUS_EXTENDED	8
#define	MII_STATUS_RESERVED	7
#define	MII_STATUS_NO_PREAMBLE	6
#define	MII_STATUS_AUTONEG_DONE	5
#define	MII_STATUS_REMOTE_FAULT	4
#define	MII_STATUS_AUTONEG_ABLE	3
#define	MII_STATUS_LINK_UP	2
#define	MII_STATUS_JABBER	1
#define	MII_STATUS_CAPABILITY	0
#define	MII_GIGA_CONTROL_FULL	9
#define	MII_GIGA_CONTROL_HALF	8
#define	MII_GIGA_STATUS_FULL	11
#define	MII_GIGA_STATUS_HALF	10
#define	MII_STATUS_1000_X_FULL	15	// Extendedn status
#define	MII_STATUS_1000_X_HALF	14
#define	MII_STATUS_1000_T_FULL	13
#define	MII_STATUS_1000_T_HALF	12

// Local/Remmote capability register
#define	MII_CAP_NEXT_PAGE	15
#define	MII_CAP_ACKNOWLEDGE	14	// Remote only
#define	MII_CAP_REMOTE_FAULT	13
#define	MII_CAP_RESERVED	12
#define	MII_CAP_ASYMM_PAUSE	11
#define	MII_CAP_SYMM_PAUSE	10
#define	MII_CAP_100BASE_T4	9
#define	MII_CAP_100BASE_TX_FULL	8
#define	MII_CAP_100BASE_TX	7
#define	MII_CAP_10BASE_TX_FULL	6
#define	MII_CAP_10BASE_TX	5
#define	MII_CAP_IEEE_802_3	0x0001

#define	MII_LINK_MODE_MASK	0x1f	// 100Base-T4, 100Base-TX and 10Base-TX

#define REALTEK_RTL8366_CHIP_ID0    0x001C
#define REALTEK_RTL8366_CHIP_ID1    0xC940
#define REALTEK_RTL8366_CHIP_ID1_MP 0xC960

#define REALTEK_MIN_PORT_ID 0
#define REALTEK_MAX_PORT_ID 5
#define REALTEK_MIN_PHY_ID REALTEK_MIN_PORT_ID
#define REALTEK_MAX_PHY_ID 4
#define REALTEK_CPU_PORT_ID REALTEK_MAX_PORT_ID
#define REALTEK_PHY_PORT_MASK ((1<<(REALTEK_MAX_PHY_ID+1)) - (1<<REALTEK_MIN_PHY_ID))
#define REALTEK_CPU_PORT_MASK (1<<REALTEK_CPU_PORT_ID)
#define REALTEK_ALL_PORT_MASK (REALTEK_PHY_PORT_MASK | REALTEK_CPU_PORT_MASK)

/* Port ability */
#define RTL8366S_PORT_ABILITY_BASE			0x0011

/* port vlan control register */
#define RTL8366S_PORT_VLAN_CTRL_BASE		0x0058

/*port linking status*/
#define RTL8366S_PORT_LINK_STATUS_BASE		0x0060
#define RTL8366S_PORT_STATUS_SPEED_BIT		0
#define RTL8366S_PORT_STATUS_SPEED_MSK		0x0003
#define RTL8366S_PORT_STATUS_DUPLEX_BIT		2
#define RTL8366S_PORT_STATUS_DUPLEX_MSK		0x0004
#define RTL8366S_PORT_STATUS_LINK_BIT		4
#define RTL8366S_PORT_STATUS_LINK_MSK		0x0010
#define RTL8366S_PORT_STATUS_TXPAUSE_BIT	5
#define RTL8366S_PORT_STATUS_TXPAUSE_MSK	0x0020
#define RTL8366S_PORT_STATUS_RXPAUSE_BIT	6
#define RTL8366S_PORT_STATUS_RXPAUSE_MSK	0x0040
#define RTL8366S_PORT_STATUS_AN_BIT			7
#define RTL8366S_PORT_STATUS_AN_MSK			0x0080

/*internal control*/
#define RTL8366S_RESET_CONTROL_REG			0x0100
#define RTL8366S_RESET_QUEUE_BIT			2

#define RTL8366S_CHIP_ID_REG				0x0105

/*MAC control*/
#define RTL8366S_MAC_FORCE_CTRL0_REG		0x0F04
#define RTL8366S_MAC_FORCE_CTRL1_REG		0x0F05


/* PHY registers control */
#define RTL8366S_PHY_ACCESS_CTRL_REG		0x8028
#define RTL8366S_PHY_ACCESS_DATA_REG		0x8029

#define RTL8366S_PHY_CTRL_READ				1
#define RTL8366S_PHY_CTRL_WRITE				0

#define RTL8366S_PHY_REG_MASK				0x1F
#define RTL8366S_PHY_PAGE_OFFSET			5
#define RTL8366S_PHY_PAGE_MASK				(0x7<<5)
#define RTL8366S_PHY_NO_OFFSET				9
#define RTL8366S_PHY_NO_MASK				(0x1F<<9)

#define RTL8366S_PHY_NO_MAX					4
#define RTL8366S_PHY_PAGE_MAX				7
#define RTL8366S_PHY_ADDR_MAX				31

/* switch global control */
#define RTL8366S_SWITCH_GLOBAL_CTRL_REG		0x0000
#define RTL8366S_MAX_LENGHT_MSK				0x0030
#define RTL8366S_MAX_LENGHT_BIT				4
#define RTL8366S_CAM_TBL_BIT				6
#define RTL8366S_JAM_MODE_BIT				9
#define RTL8366S_MAX_PAUSE_CNT_BIT			11
#define RTL8366S_EN_VLAN_BIT				13
#define RTL8366S_EN_QOS_BIT					14

/* Table Acess Control */
#define RTL8366S_TABLE_ACCESS_CTRL_REG		0x0180
#define RTL8366S_TABLE_WRITE_BASE			0x0182
#define RTL8366S_TABLE_READ_BASE			0x0188
#define RTL8366S_VLAN_TABLE_WRITE_BASE		0x0185
#define RTL8366S_VLAN_TABLE_READ_BASE		0x018b

#define RTL8366S_TABLE_VLAN_WRITE_CTRL		0x0F01	
#define RTL8366S_TABLE_VLAN_READ_CTRL		0x0E01	
#define RTL8366S_TABLE_L2TB_WRITE_CTRL		0x0101	
#define RTL8366S_TABLE_L2TB_READ_CTRL		0x0001	
#define RTL8366S_TABLE_CAMTB_WRITE_CTRL		0x0301
#define RTL8366S_TABLE_CAMTB_READ_CTRL		0x0201

/* enum for port ID */
enum PORTID
{
	PORT0 =  0,
	PORT1,
	PORT2,
	PORT3,
	PORT4,
	PORT5,
	PORT_MAX
};

/* enum for port ability speed */
enum PORTABILITYSPEED
{
	SPD_10M_H = 1,
	SPD_10M_F,
	SPD_100M_H,
	SPD_100M_F,
	SPD_1000M_F
};

/* enum for port current link speed */
enum PORTLINKSPEED
{
	SPD_10M = 0,
	SPD_100M,
	SPD_1000M
};

/* enum for mac link mode */
enum MACLINKMODE
{
	MAC_NORMAL = 0,
	MAC_FORCE,
};

/* enum for port current link duplex mode */
enum PORTLINKDUPLEXMODE
{
	HALF_DUPLEX = 0,
	FULL_DUPLEX
};

//#define LED_SET 1
//#define GET_INFO 1
#define MIB_COUNTER 1
#define CFG_SP1000 1
#define USED_DMZ	1
#ifdef USED_DMZ
#define VLAN_SET 1
#define CPU_PORT 1//FOR VLAN MUST
#define LAN_PORTID 2
#define DMZ_PORTID 3
#endif

#ifdef CPU_PORT
/* cpu port control reg */
#define RTL8366S_CPU_CTRL_REG				0x004F
#define RTL8366S_CPU_DRP_BIT				14
#define RTL8366S_CPU_DRP_MSK				0x4000
#define RTL8366S_CPU_INSTAG_BIT				15
#define RTL8366S_CPU_INSTAG_MSK				0x8000
#endif

#ifdef LED_SET
/* LED registers*/
#define RTL8366S_LED_BLINK_REG					0x420
#define RTL8366S_LED_BLINKRATE_BIT				0
#define RTL8366S_LED_BLINKRATE_MSK				0x0007
#define RTL8366S_LED_INDICATED_CONF_REG			0x421
#define RTL8366S_LED_0_1_FORCE_REG				0x422
#define RTL8366S_LED_2_3_FORCE_REG				0x423

#define RTL8366S_LED_GROUP_MAX				4
enum RTL8366S_LEDCONF
{
	LEDCONF_LEDOFF=0, 		
	LEDCONF_DUPCOL,		
	LEDCONF_LINK_ACT,		
	LEDCONF_SPD1000,		
	LEDCONF_SPD100,		
	LEDCONF_SPD10,			
	LEDCONF_SPD1000ACT,	
	LEDCONF_SPD100ACT,	
	LEDCONF_SPD10ACT,		
	LEDCONF_SPD10010ACT,  
	LEDCONF_FIBER,			
	LEDCONF_FAULT,			
	LEDCONF_LINKRX,		
	LEDCONF_LINKTX,		
	LEDCONF_MASTER,		
	LEDCONF_LEDFORCE,		
};
#endif

#ifdef VLAN_SET
/* V-LAN member configuration */
#define RTL8366S_VLAN_MEMCONF_BASE			0x0016
#define RTL8366S_VLAN_TB_CTRL_REG			0x010F
#define RTL8366S_VLAN_TB_BIT				0
#define RTL8366S_VLAN_TB_MSK				0x0001
#define RTL8366S_SPT_STATE_BASE				0x003A

#define RTL8366S_VLANMCIDXMAX				15
#define RTL8366S_FIDMAX						7
#define RTL8366S_VIDMAX						0xFFF
#define RTL8366S_PRIORITYMAX				7
#define RTL8366S_PORTMASK					0x3F
#endif

#ifdef MIB_COUNTER
/* MIBs control */
#define RTL8366S_MIB_COUTER_BASE			0x1000
#define RTL8366S_MIB_COUTER_PORT_OFFSET		0x0040
#define RTL8366S_MIB_COUTER_2_BASE			0x1180
#define RTL8366S_MIB_COUTER2_PORT_OFFSET	0x0008
#define RTL8366S_MIB_DOT1DTPLEARNDISCARD	0x11B0

#define RTL8366S_MIB_CTRL_REG				0x11F0

#define RTL8366S_MIB_CTRL_USER_MSK			0x01FF
#define RTL8366S_MIB_CTRL_BUSY_MSK			0x0001
#define RTL8366S_MIB_CTRL_RESET_MSK			0x0002

#define RTL8366S_MIB_CTRL_GLOBAL_RESET_MSK	0x0004
#define RTL8366S_MIB_CTRL_PORT_RESET_BIT	0x0003
#define RTL8366S_MIB_CTRL_PORT_RESET_MSK	0x01FC

enum RTL8366S_MIBCOUNTER{

	IfInOctets = 0,
	EtherStatsOctets,
	EtherStatsUnderSizePkts,
	EtherFregament,
	EtherStatsPkts64Octets,
	EtherStatsPkts65to127Octets,
	EtherStatsPkts128to255Octets,
	EtherStatsPkts256to511Octets,
	EtherStatsPkts512to1023Octets,
	EtherStatsPkts1024to1518Octets,
	EtherOversizeStats,
	EtherStatsJabbers,
	IfInUcastPkts,
	EtherStatsMulticastPkts,
	EtherStatsBroadcastPkts,
	EtherStatsDropEvents,
	Dot3StatsFCSErrors,
	Dot3StatsSymbolErrors,
	Dot3InPauseFrames,
	Dot3ControlInUnknownOpcodes,
	IfOutOctets,
	Dot3StatsSingleCollisionFrames,
	Dot3StatMultipleCollisionFrames,
	Dot3sDeferredTransmissions,
	Dot3StatsLateCollisions,
	EtherStatsCollisions,
	Dot3StatsExcessiveCollisions,
	Dot3OutPauseFrames,
	Dot1dBasePortDelayExceededDiscards,
	Dot1dTpPortInDiscards,
	IfOutUcastPkts,
	IfOutMulticastPkts,
	IfOutBroadcastPkts,
	/*Device only */	
	Dot1dTpLearnEntryDiscardFlag,
	RTL8366S_MIBS_NUMBER,	
};	
#endif

#define BOOL    unsigned char
#define TRUE    1
#define FALSE   0
#define SUCCESS 0
#define FAILED  1

#define sysMsDelay(_x) hal_delay_us((_x) * 1000)

BOOL rtl8366sr_phy_is_link_alive(int phyUnit);
int rtl8366sr_phy_is_up(int unit);
int rtl8366sr_phy_is_fdx(int unit);
int rtl8366sr_phy_speed(int unit);
int rtl8366sr_phy_setup(int unit);

