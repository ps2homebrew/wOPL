#ifndef __ETH_SUPPORT_H
#define __ETH_SUPPORT_H

#include "include/iosupport.h"

extern int ps2_ip_use_dhcp;
extern int ps2_ip[4];
extern int ps2_netmask[4];
extern int ps2_gateway[4];
extern int ps2_dns[4];
extern int gETHOpMode; // See ETH_OP_MODES.
extern int gPCShareAddressIsNetBIOS;
extern int pc_ip[4];
extern int gPCPort;
// Please keep these string lengths in-sync with the limits within CDVDMAN.
extern char gPCShareNBAddress[17];
extern char gPCShareName[32];
extern char gPCUserName[32];
extern char gPCPassword[32];
extern int smbCacheSize;
extern char gETHPrefix[32];
extern int gETHStartMode;

// describes what is happening in the network startup thread (>0 means loading, <0 means error)...
extern int gNetworkStartup;

void ethDeinitModules(void);      // Module-only deinitialization, without the GUI's knowledge (for specific reasons, otherwise unused).
int ethLoadInitModules(void);     // Initializes Ethernet and applies configuration.
void ethDisplayErrorStatus(void); // Displays the current error status (if any). GUI must be already initialized.
int ethGetNetConfig(u8 *ip_address, u8 *netmask, u8 *gateway);
int ethApplyConfig(void);
int ethGetDHCPStatus(void);
item_list_t *ethGetObject(int initOnly);

#endif
