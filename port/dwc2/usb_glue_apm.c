/*
 * Copyright (c) 2025, sakumisu
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "usbd_core.h"
#include "usb_dwc2_param.h"

#include "apm32f4xx_dal.h"

#ifndef CONFIG_USB_DWC2_CUSTOM_PARAM
/*
 * USB OTG FS: PA11 (DM) / PA12 (DP), dedicated full-speed core.
 * FIFO: 320 x 32-bit words (1280 byte)
 */
const struct dwc2_user_params param_pa11_pa12 = {
    .phy_type = DWC2_PHY_TYPE_PARAM_FS,
    .device_dma_enable = false,
    .device_dma_desc_enable = false,
    .device_rx_fifo_size = (320 - 16 - 16 - 16 - 16),
    .device_tx_fifo_size = {
        [0] = 16, // 64 byte
        [1] = 16, // 64 byte
        [2] = 16, // 64 byte
        [3] = 16, // 64 byte
        [4] = 0,
        [5] = 0,
        [6] = 0,
        [7] = 0,
        [8] = 0,
        [9] = 0,
        [10] = 0,
        [11] = 0,
        [12] = 0,
        [13] = 0,
        [14] = 0,
        [15] = 0 },

    // GGCCFG register (APM32 naming): bit16 = PWEN, bit21 = VBSDIS
    .device_gccfg = ((1 << 16) | (1 << 21)),

    .total_fifo_size = 320 // 1280 byte
};

#if defined(USB_OTG_HS)
/*
 * USB OTG HS: PB14 (DM) / PB15 (DP) with the on-chip full-speed PHY,
 * or the standard ULPI pins when CONFIG_USB_HS is defined (external HS PHY).
 * FIFO: 1024 x 32-bit words (4096 byte)
 */
const struct dwc2_user_params param_pb14_pb15 = {
#ifdef CONFIG_USB_HS
    .phy_type = DWC2_PHY_TYPE_PARAM_ULPI, // external ULPI PHY, 480 Mbps
#else
    .phy_type = DWC2_PHY_TYPE_PARAM_FS, // on-chip FS PHY, 12 Mbps
#endif
#ifdef CONFIG_USB_DWC2_DMA_ENABLE
    .device_dma_enable = true,
#else
    .device_dma_enable = false,
#endif
    .device_dma_desc_enable = false,
    .device_rx_fifo_size = (1006 - 16 - 256 - 128 - 128 - 128 - 128), // 1006/1012
    .device_tx_fifo_size = {
        [0] = 16,  // 64 byte
        [1] = 256, // 1024 byte
        [2] = 128, // 512 byte
        [3] = 128, // 512 byte
        [4] = 128, // 512 byte
        [5] = 128, // 512 byte
        [6] = 0,
        [7] = 0,
        [8] = 0,
        [9] = 0,
        [10] = 0,
        [11] = 0,
        [12] = 0,
        [13] = 0,
        [14] = 0,
        [15] = 0 },

    .host_dma_desc_enable = false,
    .host_rx_fifo_size = 622,
    .host_nperio_tx_fifo_size = 128, // 512 byte
    .host_perio_tx_fifo_size = 256,  // 1024 byte
#ifdef CONFIG_USB_HS
    .device_gccfg = 0,
    .host_gccfg = 0,
#else
    .device_gccfg = ((1 << 16) | (1 << 21)), // GGCCFG: PWEN | VBSDIS
    .host_gccfg = ((1 << 16) | (1 << 21))    // GGCCFG: PWEN | VBSDIS
#endif
};
#endif /* USB_OTG_HS */
#endif /* CONFIG_USB_DWC2_CUSTOM_PARAM */

typedef void (*usb_dwc2_irq)(uint8_t busid);

static usb_dwc2_irq g_usb_dwc2_irq[2];
static uint8_t g_usb_dwc2_busid[2] = { 0, 0 };

static void usb_otg_fs_gpio_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = { 0 };

    __DAL_RCM_GPIOA_CLK_ENABLE();

    /* USB DM (PA11), DP (PA12) */
    GPIO_InitStruct.Pin = GPIO_PIN_11 | GPIO_PIN_12;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF10_OTG_FS;
    DAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

static void usb_otg_fs_gpio_deinit(void)
{
    DAL_GPIO_DeInit(GPIOA, GPIO_PIN_11 | GPIO_PIN_12);
}

#if defined(USB_OTG_HS)
static void usb_otg_hs_gpio_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = { 0 };

#ifdef CONFIG_USB_HS
    /*
     * ULPI pin mapping used by the Geehy OTGD_MSC_HS1 example:
     *   PA3  -> ULPI_D0      PB10 -> ULPI_D3      PB13 -> ULPI_D6
     *   PA5  -> ULPI_CK      PB11 -> ULPI_D4      PC0  -> ULPI_STP
     *   PB0  -> ULPI_D1      PB12 -> ULPI_D5      PH4  -> ULPI_NXT
     *   PB1  -> ULPI_D2      PB5  -> ULPI_D7      PI11 -> ULPI_DIR
     * Adjust this function when your board routes the ULPI pins differently.
     */
    __DAL_RCM_GPIOA_CLK_ENABLE();
    __DAL_RCM_GPIOB_CLK_ENABLE();
    __DAL_RCM_GPIOC_CLK_ENABLE();
    __DAL_RCM_GPIOH_CLK_ENABLE();
    __DAL_RCM_GPIOI_CLK_ENABLE();

    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF10_OTG_HS;

    GPIO_InitStruct.Pin = GPIO_PIN_3 | GPIO_PIN_5;
    DAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_5 | GPIO_PIN_10 |
                          GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13;
    DAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_0;
    DAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_4;
    DAL_GPIO_Init(GPIOH, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_11;
    DAL_GPIO_Init(GPIOI, &GPIO_InitStruct);
#else
    /* USB DM (PB14), DP (PB15), on-chip full-speed PHY */
    __DAL_RCM_GPIOB_CLK_ENABLE();

    GPIO_InitStruct.Pin = GPIO_PIN_14 | GPIO_PIN_15;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF12_OTG_HS_FS;
    DAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
#endif
}

static void usb_otg_hs_gpio_deinit(void)
{
#ifdef CONFIG_USB_HS
    DAL_GPIO_DeInit(GPIOA, GPIO_PIN_3 | GPIO_PIN_5);
    DAL_GPIO_DeInit(GPIOB, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_5 | GPIO_PIN_10 |
                              GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13);
    DAL_GPIO_DeInit(GPIOC, GPIO_PIN_0);
    DAL_GPIO_DeInit(GPIOH, GPIO_PIN_4);
    DAL_GPIO_DeInit(GPIOI, GPIO_PIN_11);
#else
    DAL_GPIO_DeInit(GPIOB, GPIO_PIN_14 | GPIO_PIN_15);
#endif
}
#endif /* USB_OTG_HS */

void usb_dc_low_level_init(uint8_t busid)
{
    if (g_usbdev_bus[busid].reg_base == USB_OTG_FS_PERIPH_BASE) {
        g_usb_dwc2_busid[0] = busid;
        g_usb_dwc2_irq[0] = USBD_IRQHandler;

        usb_otg_fs_gpio_init();

        __DAL_RCM_USB_OTG_FS_CLK_ENABLE();

        DAL_NVIC_SetPriority(OTG_FS_IRQn, 1U, 0U);
        DAL_NVIC_EnableIRQ(OTG_FS_IRQn);
    }
#if defined(USB_OTG_HS)
    else if (g_usbdev_bus[busid].reg_base == USB_OTG_HS_PERIPH_BASE) {
        g_usb_dwc2_busid[1] = busid;
        g_usb_dwc2_irq[1] = USBD_IRQHandler;

        usb_otg_hs_gpio_init();

        __DAL_RCM_USB_OTG_HS_CLK_ENABLE();
#ifdef CONFIG_USB_HS
        __DAL_RCM_USB_OTG_HS_ULPI_CLK_ENABLE();
#endif

        DAL_NVIC_SetPriority(OTG_HS_IRQn, 1U, 0U);
        DAL_NVIC_EnableIRQ(OTG_HS_IRQn);
    }
#endif
}

void usb_dc_low_level_deinit(uint8_t busid)
{
    if (g_usbdev_bus[busid].reg_base == USB_OTG_FS_PERIPH_BASE) {
        g_usb_dwc2_busid[0] = 0;
        g_usb_dwc2_irq[0] = NULL;

        __DAL_RCM_USB_OTG_FS_CLK_DISABLE();

        usb_otg_fs_gpio_deinit();

        DAL_NVIC_DisableIRQ(OTG_FS_IRQn);
    }
#if defined(USB_OTG_HS)
    else if (g_usbdev_bus[busid].reg_base == USB_OTG_HS_PERIPH_BASE) {
        g_usb_dwc2_busid[1] = 0;
        g_usb_dwc2_irq[1] = NULL;

#ifdef CONFIG_USB_HS
        __DAL_RCM_USB_OTG_HS_ULPI_CLK_DISABLE();
#endif
        __DAL_RCM_USB_OTG_HS_CLK_DISABLE();

        usb_otg_hs_gpio_deinit();

        DAL_NVIC_DisableIRQ(OTG_HS_IRQn);
    }
#endif
}

#ifndef CONFIG_USB_DWC2_CUSTOM_PARAM
void dwc2_get_user_params(uint32_t reg_base, struct dwc2_user_params *params)
{
    if (reg_base == USB_OTG_FS_PERIPH_BASE) {
        memcpy(params, &param_pa11_pa12, sizeof(struct dwc2_user_params));
    }
#if defined(USB_OTG_HS)
    else if (reg_base == USB_OTG_HS_PERIPH_BASE) {
        memcpy(params, &param_pb14_pb15, sizeof(struct dwc2_user_params));
    }
#endif
    else {
        memcpy(params, &param_pa11_pa12, sizeof(struct dwc2_user_params));
    }
#ifdef CONFIG_USB_DWC2_CUSTOM_FIFO
    struct usb_dwc2_user_fifo_config s_dwc2_fifo_config;

    dwc2_get_user_fifo_config(reg_base, &s_dwc2_fifo_config);

    params->device_rx_fifo_size = s_dwc2_fifo_config.device_rx_fifo_size;
    for (uint8_t i = 0; i < MAX_EPS_CHANNELS; i++) {
        params->device_tx_fifo_size[i] = s_dwc2_fifo_config.device_tx_fifo_size[i];
    }
#endif
}
#endif

extern uint32_t SystemCoreClock;

void usbd_dwc2_delay_ms(uint8_t ms)
{
    uint32_t count = SystemCoreClock / 1000 * ms;
    while (count--) {
        __asm volatile("nop");
    }
}

uint32_t usbd_dwc2_get_system_clock(void)
{
    return SystemCoreClock;
}

void OTG_FS_IRQHandler(void)
{
    if (g_usb_dwc2_irq[0] != NULL) {
        g_usb_dwc2_irq[0](g_usb_dwc2_busid[0]);
    }
}

#if defined(USB_OTG_HS)
/* APM32 names the OTG_HS interrupt vector OTG_HS1_IRQHandler */
void OTG_HS1_IRQHandler(void)
{
    if (g_usb_dwc2_irq[1] != NULL) {
        g_usb_dwc2_irq[1](g_usb_dwc2_busid[1]);
    }
}
#endif
