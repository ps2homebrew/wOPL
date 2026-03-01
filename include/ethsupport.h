#ifndef __ETH_SUPPORT_H
#define __ETH_SUPPORT_H

#include "include/iosupport.h"

void ethDeinitModules(void);      // Module-only deinitialization, without the GUI's knowledge (for specific reasons, otherwise unused).
int ethLoadInitModules(void);     // Initializes Ethernet and applies configuration.
void ethDisplayErrorStatus(void); // Displays the current error status (if any). GUI must be already initialized.
int ethGetNetConfig(u8 *ip_address, u8 *netmask, u8 *gateway);
int ethApplyConfig(void);
int ethGetDHCPStatus(void);
item_list_t *ethGetObject(int initOnly);

#endif
