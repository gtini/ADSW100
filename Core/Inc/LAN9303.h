#ifndef LAN9303_H
#define LAN9303_H

#include "stm32h7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

// ===== PHY addresses (direct MDIO for external copper PHYs) =====
#define LAN9303_PHY1_ADDR    1     // PHY port 1
#define LAN9303_PHY2_ADDR    2     // PHY port 2

// ===== System CSR offsets (SMI mapped, 32-bit) =====
#define LAN9303_ID_REV           0x00000050UL
#define LAN9303_IRQ_CFG          0x00000054UL
#define LAN9303_IRQ_STS          0x00000058UL
#define LAN9303_IRQ_EN           0x0000005CUL

// ===== Switch CSR gateway (indirect 32-bit access via SMI) =====
#define LAN9303_SWITCH_CSR_DATA       0x000001ACUL
#define LAN9303_SWITCH_CSR_CMD        0x000001B0UL
#define LAN9303_SWITCH_CSR_CMD_BUSY   0x80000000UL  // self-clearing
#define LAN9303_SWITCH_CSR_CMD_RNW    0x40000000UL  // 1=read, 0=write
#define LAN9303_SWITCH_CSR_CMD_BE     0x000F0000UL  // byte enables (write: all bytes)

// ---- System CSR (SMI diretti, 32-bit) ----
#define LAN9303_BYTE_TEST      0x00000064UL   // attende 0x87654321
#define LAN9303_HW_CFG         0x00000074UL   // bit0=READY

// ---- Switch Fabric via gateway indiretto ----
#define LAN9303_SWE_PORT_STATE         0x00001843UL  // 00=Forwarding per P0,P1,P2
#define LAN9303_SWE_INGRESS_PORT_TYP   0x00001847UL  // 0 = no special tag

#define LAN9303_MAC_RX_CFG_0           0x00000401UL  // bit0 RX_EN
#define LAN9303_MAC_TX_CFG_0           0x00000440UL  // bit0 TX_EN

// ===== Public API =====
HAL_StatusTypeDef LAN9303_Init(void);
HAL_StatusTypeDef LAN9303_ConfigureLEDs(void);
uint8_t           LAN9303_IsLinkUp(uint8_t phy_addr);

int32_t LAN9303_GetLinkState(void);

void LAN9303_DumpPhyRegisters(uint8_t phy_addr);
void LAN9303_LogSnapshotEvery3s(void);
void LAN9303_DumpLedCfg(void);

// Indirect access to switch fabric CSRs (via gateway)
HAL_StatusTypeDef LAN9303_ReadIndirect(uint32_t switch_csr_offset, uint32_t *data);
HAL_StatusTypeDef LAN9303_WriteIndirect(uint32_t switch_csr_offset, uint32_t data);

// Direct MDIO access to copper PHYs
HAL_StatusTypeDef LAN9303_ReadPHYRegister(uint8_t phy_addr, uint8_t reg, uint16_t *value);
HAL_StatusTypeDef LAN9303_WritePHYRegister(uint8_t phy_addr, uint8_t reg, uint16_t value);

/* === Utility di debug/diagnostica === */
void LAN9303_DumpVPHY(void);            // Scansione MDIO 0..31 e stampa ID/BCR/BSR/ADV/LPA/SPC31
void LAN9303_Port0QuickCheck(void);     // Abilita RX/TX MAC0 + read-back, contatori e stato porte
void LAN9303_ReadVPHY(void);            // Legge VPHY@0 (ID1/ID2, BCR, SPC31 con decodifica)
void LAN9303_CheckPort0(void);          // Contatori Port0: RX_OK(0x0418) e TX_OK(0x0453)
void LAN9303_Port0QuickCheckDelta(void);    // Logga RX_OK/TX_OK di Port0 solo su variazione (Δ)
void LAN9303_Port0RxErrorsDelta(void);      // Logga le Δ degli errori RX di Port0
void ETH_PrintMemMap(void);

#ifdef __cplusplus
}
#endif

#endif // LAN9303_H
