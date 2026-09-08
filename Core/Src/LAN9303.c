
#include "h_files.h"
#include "eth_custom_phy_interface.h"

extern ETH_HandleTypeDef heth;

/* ==================== LOG ==================== */
#ifndef LOG
#define LOG USB_Printf
#endif

/* ==================== ACCESSORS  ==================== */
static inline uint16_t MDIO_Read(uint8_t phy, uint8_t reg)
{
    uint16_t val = 0;
    if (LAN9303_ReadPHYRegister(phy, reg, &val) != HAL_OK)
    {
        LOG("MDIO_Read ERR phy=%u reg=%u\r\n", phy, reg);
    }
    return val;
}

static inline void MDIO_Write(uint8_t phy, uint8_t reg, uint16_t value)
{
    if (LAN9303_WritePHYRegister(phy, reg, value) != HAL_OK)
    {
        LOG("MDIO_Write ERR phy=%u reg=%u val=0x%04X\r\n", phy, reg, value);
    }
}

static inline uint32_t SCSR_Read(uint32_t csr_offset)
{
    uint32_t v = 0;
    if (LAN9303_ReadIndirect(csr_offset, &v) != HAL_OK)
    {
        LOG("SCSR_Read ERR off=0x%04lX\r\n", csr_offset);
    }
    return v;
}

static inline void SCSR_Write(uint32_t csr_offset, uint32_t val)
{
    if (LAN9303_WriteIndirect(csr_offset, val) != HAL_OK)
    {
        LOG("SCSR_Write ERR off=0x%04lX val=0x%08lX\r\n", csr_offset, val);
    }
}

void LAN9303_CheckPort0(void)
{
    uint32_t rx_ok=0, tx_ok=0;

    LAN9303_ReadIndirect(0x0418, &rx_ok);   // MAC_RX_PKTOK_CNT_0
    LAN9303_ReadIndirect(0x0453, &tx_ok);   // MAC_TX_PKTOK_CNT_0  << CORRETTO

    USB_Printf("P0 counters  RX_OK:%lu  TX_OK:%lu\r\n", rx_ok, tx_ok);
}

void LAN9303_ReadVPHY(void)
{
    uint16_t vphy_bcr=0, vphy_spc31=0, id1=0, id2=0;

    // VPHY @ addr 0 con phy_addr_sel_strap=0
    if (LAN9303_ReadPHYRegister(0, 2, &id1) == HAL_OK &&
        LAN9303_ReadPHYRegister(0, 3, &id2) == HAL_OK &&
        LAN9303_ReadPHYRegister(0, 0, &vphy_bcr) == HAL_OK &&
        LAN9303_ReadPHYRegister(0, 31, &vphy_spc31) == HAL_OK)
    {
        uint8_t mode = (uint8_t)((vphy_spc31 >> 8) & 0x3); // [9:8]
        uint8_t clkdir = (uint8_t)((vphy_spc31 >> 6) & 0x1);

        USB_Printf("VPHY@0  ID1:0x%04X ID2:0x%04X  BCR:0x%04X  SPC31:0x%04X\r\n",
                   id1, id2, vphy_bcr, vphy_spc31);
        USB_Printf("  Mode[9:8]=%u (atteso 2=RMII PHY),  RMII_ClkDir(bit6)=%u (1=OUT)\r\n",
                   mode, clkdir);
        USB_Printf("  ISO(bit10 in BCR)=%u (0=non isolato)\r\n", (uint8_t)((vphy_bcr>>10)&1));
    }
    else
    {
        USB_Printf("VPHY read failed (addr 0)\r\n");
    }
}

/* ==================== DUMP VIRTUAL PHY ==================== */
// Nota: identifichiamo il VPHY scansionando 0..31 e stampiamo reg chiave.
// La decodifica bit-per-bit di reg31 (RMII mode / clock dir) la leggiamo dal valore grezzo per evitare ambiguità di bitfield tra revisioni.
static int vphy_id_valid(uint16_t id1, uint16_t id2)
{
    if ((id1 == 0xFFFF && id2 == 0xFFFF) || (id1 == 0x0000 && id2 == 0x0000)) return 0;
    return 1;
}

void LAN9303_DumpVPHY(void)
{
    LOG("\r\n=== VPHY SCAN ===\r\n");
    for (uint8_t phy = 0; phy < 32; ++phy) {
        uint16_t id1 = MDIO_Read(phy, 2);  // PHYID1
        uint16_t id2 = MDIO_Read(phy, 3);  // PHYID2
        if (!vphy_id_valid(id1, id2)) continue;

        uint16_t bcr  = MDIO_Read(phy, 0);   // Basic Control
        uint16_t bsr  = MDIO_Read(phy, 1);   // Basic Status
        uint16_t adv  = MDIO_Read(phy, 4);   // Advertise
        uint16_t lpa  = MDIO_Read(phy, 5);   // Link Partner Ability
        uint16_t spc  = MDIO_Read(phy, 31);  // Special Control/Status (VPHY: RMII mode/clock dir)

        LOG("PHY@%u  ID1:0x%04X ID2:0x%04X  BCR:0x%04X BSR:0x%04X ADV:0x%04X LPA:0x%04X SPC31:0x%04X\r\n",
            phy, id1, id2, bcr, bsr, adv, lpa, spc);
    }
    LOG("Nota: il VPHY è quello il cui reg31 (SPC31) governa RMII mode/clock direction.\r\n");
}

/* ==================== PORT0 QUICK CHECK ==================== */
// Offsets CSR indiretti (adatta se nel tuo header hai nomi/offset diversi)
#ifndef MAC_RX_CFG_0
#define MAC_RX_CFG_0         0x0400u
#endif
#ifndef MAC_TX_CFG_0
#define MAC_TX_CFG_0         0x0440u
#endif
#ifndef SWE_PORT_STATE
#define SWE_PORT_STATE       0x1843u
#endif
// Contatori: usa i tuoi define se già li hai, altrimenti questi sono tipici per "OK pkt"
#ifndef MAC_RX_PKTOK_CNT_0
#define MAC_RX_PKTOK_CNT_0   0x0418u
#endif
#ifndef MAC_TX_PKTOK_CNT_0
#define MAC_TX_PKTOK_CNT_0   0x0453u
#endif

void LAN9303_Port0QuickCheck(void)
{
    uint32_t rx_cfg = SCSR_Read(MAC_RX_CFG_0);
    uint32_t tx_cfg = SCSR_Read(MAC_TX_CFG_0);
    LOG("MAC_RX_CFG_0(before)=0x%08lX  MAC_TX_CFG_0(before)=0x%08lX\r\n", rx_cfg, tx_cfg);

    // Abilita bit0 (RX/TX enable) + read-back
    rx_cfg |= 0x00000001u;
    tx_cfg |= 0x00000001u;
    SCSR_Write(MAC_RX_CFG_0, rx_cfg);
    SCSR_Write(MAC_TX_CFG_0, tx_cfg);

    uint32_t rx_cfg_rb = SCSR_Read(MAC_RX_CFG_0);
    uint32_t tx_cfg_rb = SCSR_Read(MAC_TX_CFG_0);
    LOG("MAC_RX_CFG_0(after)=0x%08lX  MAC_TX_CFG_0(after)=0x%08lX\r\n", rx_cfg_rb, tx_cfg_rb);

    uint32_t port_state = SCSR_Read(SWE_PORT_STATE);
    LOG("SWE_PORT_STATE=0x%08lX (0=all forwarding)\r\n", port_state);

    // Contatori prima
    uint32_t rx0_a = SCSR_Read(MAC_RX_PKTOK_CNT_0);
    uint32_t tx0_a = SCSR_Read(MAC_TX_PKTOK_CNT_0);
    LOG("P0 counters BEFORE  RX_OK:%lu  TX_OK:%lu\r\n", rx0_a, tx0_a);

    HAL_Delay(200); // tempo per qualche frame (DHCP/ARP/ping)

    // Contatori dopo
    uint32_t rx0_b = SCSR_Read(MAC_RX_PKTOK_CNT_0);
    uint32_t tx0_b = SCSR_Read(MAC_TX_PKTOK_CNT_0);
    LOG("P0 counters AFTER   RX_OK:%lu  TX_OK:%lu\r\n", rx0_b, tx0_b);
}





// ------------------------
//  Low-level SMI helpers
// ------------------------
// Map 10-bit CSR offset into MDIO (virtual) PHY address and register index per AN2433.
// Upper-half MDIO space is used (bit9=1). bit0 selects HI(1)/LO(0) word.
static HAL_StatusTypeDef LAN9303_SMI_Read16(uint16_t csr_off_10bit, uint8_t hi, uint16_t *out)
{
    uint32_t tmp = csr_off_10bit & 0x03FFUL;
    tmp >>= 1;
    tmp |= 0x200UL;               // set bit9 -> upper half (PHY_ADDR bit4 = 1)
    if (hi) tmp |= 0x001UL;       // select HI word

    uint16_t phy_addr = (uint16_t)((tmp & 0x3E0UL) >> 5); // bits 9..5
    uint16_t reg_idx  = (uint16_t)(tmp & 0x1FUL);         // bits 4..0

    uint32_t val32 = 0;
    HAL_StatusTypeDef st = HAL_ETH_ReadPHYRegister(&heth, phy_addr, reg_idx, &val32);
    if (st == HAL_OK && out) *out = (uint16_t)(val32 & 0xFFFF);
    return st;
}

static HAL_StatusTypeDef LAN9303_SMI_Write16(uint16_t csr_off_10bit, uint8_t hi, uint16_t val)
{
    uint32_t tmp = csr_off_10bit & 0x03FFUL;
    tmp >>= 1;
    tmp |= 0x200UL;               // set bit9 -> upper half
    if (hi) tmp |= 0x001UL;       // HI word

    uint16_t phy_addr = (uint16_t)((tmp & 0x3E0UL) >> 5);
    uint16_t reg_idx  = (uint16_t)(tmp & 0x1FUL);

    return HAL_ETH_WritePHYRegister(&heth, phy_addr, reg_idx, val);
}

// Read/Write 32-bit System CSR via SMI mapping
static HAL_StatusTypeDef LAN9303_SMI_Read32(uint32_t csr_offset, uint32_t *data)
{
    uint16_t lo, hi;
    uint16_t off10 = (uint16_t)(csr_offset & 0x03FFUL);

    HAL_StatusTypeDef st;
    st = LAN9303_SMI_Read16(off10, 1, &hi); if (st != HAL_OK) return st;
    st = LAN9303_SMI_Read16(off10, 0, &lo); if (st != HAL_OK) return st;

    if (data) *data = (((uint32_t)hi) << 16) | (uint32_t)lo;
    return HAL_OK;
}

static HAL_StatusTypeDef LAN9303_SMI_Write32(uint32_t csr_offset, uint32_t data)
{
    uint16_t off10 = (uint16_t)(csr_offset & 0x03FFUL);
    uint16_t lo = (uint16_t)(data & 0xFFFF);
    uint16_t hi = (uint16_t)(data >> 16);

    HAL_StatusTypeDef st;
    st = LAN9303_SMI_Write16(off10, 0, lo); if (st != HAL_OK) return st; // write LO first
    st = LAN9303_SMI_Write16(off10, 1, hi); if (st != HAL_OK) return st; // then HI
    return HAL_OK;
}

// ------------------------
//  Switch CSR gateway
// ------------------------
static HAL_StatusTypeDef LAN9303_SwitchWaitNotBusy(void)
{
    const uint32_t timeout_ms = 2;  // timeout breve per non bloccare il main
    const uint32_t t0 = HAL_GetTick();
    while ((HAL_GetTick() - t0) < timeout_ms)
    {
        uint32_t cmd = 0;
        if (LAN9303_SMI_Read32(LAN9303_SWITCH_CSR_CMD, &cmd) != HAL_OK)
            return HAL_ERROR;
        if ((cmd & LAN9303_SWITCH_CSR_CMD_BUSY) == 0U)
            return HAL_OK;
    }
    return HAL_TIMEOUT;
}

HAL_StatusTypeDef LAN9303_ReadIndirect(uint32_t switch_csr_offset, uint32_t *data)
{
    HAL_StatusTypeDef st;

    st = LAN9303_SwitchWaitNotBusy();
    if (st != HAL_OK) return st;

    uint32_t cmd = (switch_csr_offset & 0x000FFFFFUL)   // 20-bit addr (spec)
                 | LAN9303_SWITCH_CSR_CMD_BUSY
                 | LAN9303_SWITCH_CSR_CMD_RNW;          // Read

    if (LAN9303_SMI_Write32(LAN9303_SWITCH_CSR_CMD, cmd) != HAL_OK) return HAL_ERROR;

    st = LAN9303_SwitchWaitNotBusy();
    if (st != HAL_OK) return st;

    return LAN9303_SMI_Read32(LAN9303_SWITCH_CSR_DATA, data);
}

HAL_StatusTypeDef LAN9303_WriteIndirect(uint32_t switch_csr_offset, uint32_t data)
{
    HAL_StatusTypeDef st;

    st = LAN9303_SwitchWaitNotBusy();
    if (st != HAL_OK) return st;

    if (LAN9303_SMI_Write32(LAN9303_SWITCH_CSR_DATA, data) != HAL_OK) return HAL_ERROR;

    uint32_t cmd = (switch_csr_offset & 0x000FFFFFUL)   // 20-bit addr (spec)
                 | LAN9303_SWITCH_CSR_CMD_BUSY
                 #ifdef LAN9303_SWITCH_CSR_CMD_BE
                 | LAN9303_SWITCH_CSR_CMD_BE   // Write (BE=0xF<<pos)
#endif
                 ;

    if (LAN9303_SMI_Write32(LAN9303_SWITCH_CSR_CMD, cmd) != HAL_OK) return HAL_ERROR;

    return LAN9303_SwitchWaitNotBusy();
}

// ------------------------
//  Direct PHY access
// ------------------------
HAL_StatusTypeDef LAN9303_ReadPHYRegister(uint8_t phy_addr, uint8_t reg, uint16_t *value)
{
    uint32_t temp = 0;
    HAL_StatusTypeDef st = HAL_ERROR;
    for (uint32_t i = 0; i < 3; ++i) {
        st = HAL_ETH_ReadPHYRegister(&heth, phy_addr, reg, &temp);
        if (st == HAL_OK) break;
        if (st != HAL_BUSY) break;
    }
    if (st == HAL_OK && value) *value = (uint16_t)(temp & 0xFFFFu);
    return st;
}

HAL_StatusTypeDef LAN9303_WritePHYRegister(uint8_t phy_addr, uint8_t reg, uint16_t value)
{
    HAL_StatusTypeDef st = HAL_ERROR;
    for (uint32_t i = 0; i < 3; ++i) {
        st = HAL_ETH_WritePHYRegister(&heth, phy_addr, reg, (uint32_t)value);
        if (st == HAL_OK) break;
        if (st != HAL_BUSY) break;
    }
    return st;
}

// ------------------------
//  Optional: MDIO scan (debug)
// ------------------------
static void LAN9303_ScanMDIO(void)
{
    uint32_t val2, val3;
    USB_Printf("== Scansione MDIO (0..31) ==\n");
    for (uint8_t addr = 0; addr < 32; addr++)
    {
        if (HAL_ETH_ReadPHYRegister(&heth, addr, 0x02, &val2) == HAL_OK &&
            HAL_ETH_ReadPHYRegister(&heth, addr, 0x03, &val3) == HAL_OK)
        {
            uint16_t id_hi = (uint16_t)(val2 & 0xFFFF);
            uint16_t id_lo = (uint16_t)(val3 & 0xFFFF);
            if ((id_hi != 0xFFFF && id_hi != 0x0000) || (id_lo != 0xFFFF && id_lo != 0x0000))
            {
                USB_Printf("PHY @0x%02X  ID1=0x%04X ID2=0x%04X\n", addr, id_hi, id_lo);
            }
        }
    }
    USB_Printf("== Fine scansione ==\n");
}

// ------------------------
//  LEDs
// ------------------------
HAL_StatusTypeDef LAN9303_ConfigureLEDs(void)
{
    uint32_t led = 0;

    USB_Printf("Configuring LAN9303 PHY LEDs...\r\n");

    if (LAN9303_SMI_Read32(0x1BC, &led) != HAL_OK) {
        USB_Printf("ERR: read LED_CFG\r\n");
        return HAL_ERROR;
    }
    USB_Printf("LED_CFG before = 0x%08lX\r\n", led);

    // LED_FUN = 10b
    led &= ~(3u << 8);
    led |=  (2u << 8);

    // abilita LED0,1,3,4
    led &= ~0x3Fu;
    led |=  (1u << 0) | (1u << 1) | (1u << 3) | (1u << 4);

    if (LAN9303_SMI_Write32(0x1BC, led) != HAL_OK) {
        USB_Printf("ERR: write LED_CFG\r\n");
        return HAL_ERROR;
    }

    if (LAN9303_SMI_Read32(0x1BC, &led) == HAL_OK) {
        USB_Printf("LED_CFG after  = 0x%08lX\r\n", led);
    }

    return HAL_OK;
}

void LAN9303_DumpLedCfg(void)
{
    uint32_t led = 0;
    if (LAN9303_SMI_Read32(0x1BC, &led) != HAL_OK)
    {
        USB_Printf("LAN9303: LED_CFG read ERR\r\n");
        return;
    }

    uint32_t fun = (led >> 8) & 0x3u;   // LED_FUN[9:8]
    uint32_t en  =  led        & 0x3Fu; // LED_EN[5:0]

    USB_Printf("LAN9303 LED_CFG = 0x%08lX\r\n", led);
    USB_Printf("  LED_FUN[9:8] = %lu\r\n", fun);
    USB_Printf("  LED_EN[5:0]  = 0x%02lX\r\n", en);

    const char *fun_desc = "unknown";
    switch (fun) {
        case 0: fun_desc = "0 = Link/Act + Duplex/Collision"; break;
        case 1: fun_desc = "1 = Link/Act + 100Link/Act";      break;
        case 2: fun_desc = "2 = Link/Act + Speed";            break;
        case 3: fun_desc = "3 = GPIO mode";                   break;
    }
    USB_Printf("  Mode = %s\r\n", fun_desc);

    USB_Printf("  Enabled LEDs:");
    if (en & (1u << 0)) USB_Printf(" LED0");
    if (en & (1u << 1)) USB_Printf(" LED1");
    if (en & (1u << 2)) USB_Printf(" LED2");
    if (en & (1u << 3)) USB_Printf(" LED3");
    if (en & (1u << 4)) USB_Printf(" LED4");
    if (en & (1u << 5)) USB_Printf(" LED5");
    USB_Printf("\r\n");
}

// ------------------------
//  Init
// ------------------------
HAL_StatusTypeDef LAN9303_Init(void)
{
    USB_Printf("Init LAN9303...\n");

    // (facoltativo) scansione MDIO
    LAN9303_ScanMDIO();

    // 0) Attendi che il device sia pronto (BYTE_TEST / HW_CFG)
    uint32_t v = 0;
    for (int i = 0; i < 100; i++) {
        if (LAN9303_SMI_Read32(LAN9303_BYTE_TEST, &v) == HAL_OK && v == 0x87654321UL) break;
        HAL_Delay(1);
    }
    USB_Printf("BYTE_TEST=0x%08lX\n", v);

    for (int i = 0; i < 100; i++) {
        if (LAN9303_SMI_Read32(LAN9303_HW_CFG, &v) == HAL_OK && (v & 0x00000001UL)) break; // bit0 READY
        HAL_Delay(1);
    }
    USB_Printf("HW_CFG   =0x%08lX\n", v);

    // 1) ID/REV
    uint32_t idrev = 0;
    if (LAN9303_SMI_Read32(LAN9303_ID_REV, &idrev) != HAL_OK) {
        USB_Printf("Errore lettura ID_REV\n");
        return HAL_ERROR;
    }
    USB_Printf("LAN9303 ID_REV: 0x%08lX\n", idrev);

    // 2) IRQ: clear/disable (così non disturbano il bring-up)
    (void)LAN9303_SMI_Read32(LAN9303_IRQ_STS, &v);     // read-to-clear
    (void)LAN9303_SMI_Write32(LAN9303_IRQ_EN,  0x00000000UL);
    (void)LAN9303_SMI_Write32(LAN9303_IRQ_CFG, 0x00000000UL);

    // 3) Switch fabric: tutte le porte in Forwarding, nessun special tag su ingresso
    (void)LAN9303_WriteIndirect(LAN9303_SWE_PORT_STATE, 0x00000000UL);        // 00=Forwarding per P0,P1,P2
    (void)LAN9303_WriteIndirect(LAN9303_SWE_INGRESS_PORT_TYP, 0x00000000UL);  // 0 = normal (no special tag)

    // 4) Abilita MAC Host Port (Port 0) RX e TX
    (void)LAN9303_WriteIndirect(LAN9303_MAC_RX_CFG_0, 0x00000001UL);  // RX_EN
    (void)LAN9303_WriteIndirect(LAN9303_MAC_TX_CFG_0, 0x00000001UL);  // TX_EN

    // 5) Read-back diagnostico
    uint32_t back = 0;
    (void)LAN9303_ReadIndirect(LAN9303_SWE_PORT_STATE, &back);
    USB_Printf("SWE_PORT_STATE        = 0x%08lX\n", back);
    (void)LAN9303_ReadIndirect(LAN9303_SWE_INGRESS_PORT_TYP, &back);
    USB_Printf("SWE_INGRESS_PORT_TYP  = 0x%08lX\n", back);
    (void)LAN9303_ReadIndirect(LAN9303_MAC_RX_CFG_0, &back);
    USB_Printf("MAC_RX_CFG_0          = 0x%08lX\n", back);
    (void)LAN9303_ReadIndirect(LAN9303_MAC_TX_CFG_0, &back);
    USB_Printf("MAC_TX_CFG_0          = 0x%08lX\n", back);

    // 6) Riavvia auto-neg sui due PHY rame
    (void)LAN9303_WritePHYRegister(LAN9303_PHY1_ADDR, 0x00, 0x1200);  // BMCR: Restart AN
    (void)LAN9303_WritePHYRegister(LAN9303_PHY2_ADDR, 0x00, 0x1200);
    USB_Printf("LAN9303: PHY auto-negotiation avviata.\n");

    // 7) Stato link iniziale (double read BMSR per latch)
    uint16_t b1a=0,b1b=0,b2a=0,b2b=0;
    LAN9303_ReadPHYRegister(LAN9303_PHY1_ADDR, 0x01, &b1a);
    LAN9303_ReadPHYRegister(LAN9303_PHY1_ADDR, 0x01, &b1b);
    LAN9303_ReadPHYRegister(LAN9303_PHY2_ADDR, 0x01, &b2a);
    LAN9303_ReadPHYRegister(LAN9303_PHY2_ADDR, 0x01, &b2b);
    USB_Printf("PHY1 BMSR: 0x%04X -> LINK %s\n", (uint16_t)(b1a|b1b), ((b1a|b1b)&0x0004)?"UP":"DOWN");
    USB_Printf("PHY2 BMSR: 0x%04X -> LINK %s\n", (uint16_t)(b2a|b2b), ((b2a|b2b)&0x0004)?"UP":"DOWN");

    LAN9303_ConfigureLEDs();

    return HAL_OK;
}

int32_t LAN9303_GetLinkState(void)
{
    int32_t linkstate = USER_PHY_STATUS_LINK_DOWN;
    static int32_t prev = 9999;

    uint16_t bmsr1a = 0, bmsr1b = 0;
    uint16_t bmsr2a = 0, bmsr2b = 0;
    uint16_t scsr1  = 0, scsr2  = 0;

    /* BMSR: doppia lettura per latch-clear (PHY1 e PHY2) */
    LAN9303_ReadPHYRegister(LAN9303_PHY1_ADDR, 0x01u, &bmsr1a);
    LAN9303_ReadPHYRegister(LAN9303_PHY1_ADDR, 0x01u, &bmsr1b);
    LAN9303_ReadPHYRegister(LAN9303_PHY2_ADDR, 0x01u, &bmsr2a);
    LAN9303_ReadPHYRegister(LAN9303_PHY2_ADDR, 0x01u, &bmsr2b);

    const bool link1 = (((uint16_t)(bmsr1a | bmsr1b)) & 0x0004u) != 0u;
    const bool link2 = (((uint16_t)(bmsr2a | bmsr2b)) & 0x0004u) != 0u;

    uint8_t  active_phy = 0xFF;
    uint16_t scsr       = 0;

    /* Scegli il PHY "attivo" da cui prendere speed/duplex
       (priorità PHY1, altrimenti PHY2) */
    if (link1)
    {
        active_phy = LAN9303_PHY1_ADDR;
        LAN9303_ReadPHYRegister(LAN9303_PHY1_ADDR, 0x1Fu, &scsr1);
        scsr = scsr1;
    }
    else if (link2)
    {
        active_phy = LAN9303_PHY2_ADDR;
        LAN9303_ReadPHYRegister(LAN9303_PHY2_ADDR, 0x1Fu, &scsr2);
        scsr = scsr2;
    }

    if (!link1 && !link2)
    {
        /* nessun cavo collegato */
        linkstate = USER_PHY_STATUS_LINK_DOWN;
    }
    else
    {
        /* almeno una porta UP -> decodifica speed/duplex dal PHY attivo */
        const bool fast = (scsr & (1u << 4)) != 0u; // 1=100M, 0=10M
        const bool full = (scsr & (1u << 3)) != 0u; // 1=Full, 0=Half

        if (fast && full)      linkstate = USER_PHY_STATUS_100MBITS_FULLDUPLEX;
        else if (fast)         linkstate = USER_PHY_STATUS_100MBITS_HALFDUPLEX;
        else if (full)         linkstate = USER_PHY_STATUS_10MBITS_FULLDUPLEX;
        else                   linkstate = USER_PHY_STATUS_10MBITS_HALFDUPLEX;
    }

    if (linkstate != prev)
    {
        USB_Printf("LAN9303 link -> %ld (P1:%s P2:%s SCSR1=%04X SCSR2=%04X, active PHY=%d)\r\n",
                   (long)linkstate,
                   link1 ? "UP" : "DOWN",
                   link2 ? "UP" : "DOWN",
                   scsr1, scsr2,
                   (int)active_phy);
        prev = linkstate;
    }

    return linkstate;
}

// ------------------------
//  Periodic debug dump
// ------------------------
static uint8_t LAN9303_LinkUp_BMSR(uint8_t phy_addr, uint16_t *bmsr_out)
{
    uint16_t dummy, bmsr;
    if (LAN9303_ReadPHYRegister(phy_addr, 0x01, &dummy) != HAL_OK) return 0;
    if (LAN9303_ReadPHYRegister(phy_addr, 0x01, &bmsr)  != HAL_OK) return 0;
    if (bmsr_out) *bmsr_out = bmsr;
    return (bmsr & 0x0004U) ? 1U : 0U;
}

void LAN9303_DumpPhyRegisters(uint8_t phy_addr)
{
    for (uint8_t reg = 0x00; reg <= 0x1F; reg++)
    {
        uint16_t dummy, val;
        if (LAN9303_ReadPHYRegister(phy_addr, reg, &dummy) != HAL_OK) { USB_Printf("PHY%u[%02X]=ERR ", phy_addr, reg); continue; }
        if (LAN9303_ReadPHYRegister(phy_addr, reg, &val)   != HAL_OK) { USB_Printf("PHY%u[%02X]=ERR ", phy_addr, reg); continue; }
        USB_Printf("PHY%u[%02X]=0x%04X ", phy_addr, reg, val);
        if (((reg+1) % 8) == 0) USB_Printf("\n");
    }
    if ((0x1F+1) % 8) USB_Printf("\n");
}

void LAN9303_LogSnapshotEvery3s(void)
{
    static uint32_t t_prev = 0;
    uint32_t now = HAL_GetTick();
    if ((now - t_prev) < 3000) return;
    t_prev = now;

    uint16_t bmsr1=0, bmsr2=0, bmcr1=0, bmcr2=0, scs1=0, scs2=0; // 0x10 = Special Control/Status
    LAN9303_ReadPHYRegister(LAN9303_PHY1_ADDR, 0x00, &bmcr1);
    LAN9303_ReadPHYRegister(LAN9303_PHY2_ADDR, 0x00, &bmcr2);

    // Read 0x10 with double-read as well
    uint16_t dmy;
    LAN9303_ReadPHYRegister(LAN9303_PHY1_ADDR, 0x10, &dmy);
    LAN9303_ReadPHYRegister(LAN9303_PHY1_ADDR, 0x10, &scs1);
    LAN9303_ReadPHYRegister(LAN9303_PHY2_ADDR, 0x10, &dmy);
    LAN9303_ReadPHYRegister(LAN9303_PHY2_ADDR, 0x10, &scs2);

    uint8_t link1 = LAN9303_LinkUp_BMSR(LAN9303_PHY1_ADDR, &bmsr1);
    uint8_t link2 = LAN9303_LinkUp_BMSR(LAN9303_PHY2_ADDR, &bmsr2);

    USB_Printf("\n----- LAN9303 Snapshot (%lu ms) -----\n", (unsigned long)now);
    USB_Printf("PHY1: BMCR=0x%04X BMSR=0x%04X SCS(0x10)=0x%04X LINK=%s\n",
               bmcr1, bmsr1, scs1, link1 ? "UP" : "DOWN");
    USB_Printf("PHY2: BMCR=0x%04X BMSR=0x%04X SCS(0x10)=0x%04X LINK=%s\n",
               bmcr2, bmsr2, scs2, link2 ? "UP" : "DOWN");

    // Core CSRs (via SMI)
    uint32_t idrev=0, irq_sts=0, irq_cfg=0, irq_en=0, sw_ctrl=0;
    if (LAN9303_SMI_Read32(LAN9303_ID_REV,  &idrev)   == HAL_OK) USB_Printf("ID_REV       : 0x%08lX\n", idrev);
    if (LAN9303_SMI_Read32(LAN9303_IRQ_STS, &irq_sts) == HAL_OK) USB_Printf("IRQ_STS      : 0x%08lX\n", irq_sts);
    if (LAN9303_SMI_Read32(LAN9303_IRQ_CFG, &irq_cfg) == HAL_OK) USB_Printf("IRQ_CFG      : 0x%08lX\n", irq_cfg);
    if (LAN9303_SMI_Read32(LAN9303_IRQ_EN,  &irq_en)  == HAL_OK) USB_Printf("IRQ_EN       : 0x%08lX\n", irq_en);
    if (LAN9303_ReadIndirect(0x00000050UL,  &sw_ctrl) == HAL_OK) USB_Printf("SWITCH_CTRL  : 0x%08lX\n", sw_ctrl);

    USB_Printf("---- Dump PHY1 (0x00..0x1F) ----\n");
    LAN9303_DumpPhyRegisters(LAN9303_PHY1_ADDR);
    USB_Printf("---- Dump PHY2 (0x00..0x1F) ----\n");
    LAN9303_DumpPhyRegisters(LAN9303_PHY2_ADDR);
}
/* ==========================================================================
 * Debug helpers (Port0 counters on change) -- remove when no longer needed
 * ========================================================================= */
#ifndef LOG
#define LOG USB_Printf
#endif

#ifndef MAC_RX_PKTOK_CNT_0
#define MAC_RX_PKTOK_CNT_0   0x0418u  /* RX Packet OK Port0 (CPU -> Switch) */
#endif
#ifndef MAC_TX_PKTOK_CNT_0
#define MAC_TX_PKTOK_CNT_0   0x0453u  /* TX Packet OK Port0 (Switch -> CPU) */
#endif

void LAN9303_Port0QuickCheckDelta(void)
{
    static uint8_t  init = 0;
    static uint32_t tot_rx = 0, tot_tx = 0;  // 32-bit bastano per il debug

    uint32_t rx = 0, tx = 0;
    if (LAN9303_ReadIndirect(0x0418, &rx) != HAL_OK) return; // RX_OK P0 (clear-on-read)
    if (LAN9303_ReadIndirect(0x0453, &tx) != HAL_OK) return; // TX_OK P0 (clear-on-read)

    tot_rx += rx;
    tot_tx += tx;

    if (!init) {
        init = 1;
        USB_Printf("P0 dRX:%lu dTX:%lu (totRX:%lu totTX:%lu)\r\n", rx, tx, tot_rx, tot_tx);
        return;
    }

    if (rx || tx) {
        USB_Printf("P0 dRX:%lu dTX:%lu (totRX:%lu totTX:%lu)\r\n", rx, tx, tot_rx, tot_tx);
    }
}

#ifndef MAC_RX_CRCERR_CNT_0
#define MAC_RX_CRCERR_CNT_0    0x0419u
#endif
#ifndef MAC_RX_ALIGNERR_CNT_0
#define MAC_RX_ALIGNERR_CNT_0  0x041Fu
#endif
#ifndef MAC_RX_FRAGERR_CNT_0
#define MAC_RX_FRAGERR_CNT_0   0x041Du
#endif
#ifndef MAC_RX_SYMBLERR_CNT_0
#define MAC_RX_SYMBLERR_CNT_0  0x0422u
#endif

void LAN9303_Port0RxErrorsDelta(void)
{
    static uint8_t  init = 0;
    static uint32_t p_crc=0, p_aln=0, p_frag=0, p_sym=0;

    uint32_t c_crc=0, c_aln=0, c_frag=0, c_sym=0;
    if (LAN9303_ReadIndirect(MAC_RX_CRCERR_CNT_0,   &c_crc)  != HAL_OK) return;
    if (LAN9303_ReadIndirect(MAC_RX_ALIGNERR_CNT_0, &c_aln)  != HAL_OK) return;
    if (LAN9303_ReadIndirect(MAC_RX_FRAGERR_CNT_0,  &c_frag) != HAL_OK) return;
    if (LAN9303_ReadIndirect(MAC_RX_SYMBLERR_CNT_0, &c_sym)  != HAL_OK) return;

    if (!init) { init=1; p_crc=c_crc; p_aln=c_aln; p_frag=c_frag; p_sym=c_sym; return; }

    uint32_t d_crc  = c_crc  - p_crc;
    uint32_t d_aln  = c_aln  - p_aln;
    uint32_t d_frag = c_frag - p_frag;
    uint32_t d_sym  = c_sym  - p_sym;

    if (d_crc || d_aln || d_frag || d_sym) {
        LOG("P0 RX_ERR Δ CRC:%lu ALN:%lu FRAG:%lu SYM:%lu\r\n", d_crc, d_aln, d_frag, d_sym);
        p_crc=c_crc; p_aln=c_aln; p_frag=c_frag; p_sym=c_sym;
    }
}

// Word2 del descrittore RX = indirizzo del primo buffer (H7)
static inline uint32_t ETH_GetRxDesc0BufAddr(void) {
    volatile uint32_t *d = (uint32_t *)&heth.RxDescList.RxDesc[0];
    return d[2]; // DES2 = Buffer1 address
}

void ETH_PrintMemMap(void)
{
    uint32_t rxdesc0 = (uint32_t)&heth.RxDescList.RxDesc[0];
    uint32_t txdesc0 = (uint32_t)&heth.TxDescList.TxDesc[0];
    uint32_t rxbuf0  = ETH_GetRxDesc0BufAddr();

    USB_Printf("RxDesc0@%08lX  TxDesc0@%08lX  RxBuf0@%08lX\r\n",
               rxdesc0, txdesc0, rxbuf0);
}
