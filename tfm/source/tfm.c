/**
 * @file    tfm.c
 * @brief   Application entry point.
 */

/*******************************************************************************
 * Includes
 ******************************************************************************/
#include <stdio.h>
#include <stdlib.h>

#include "board.h"

#include "peripherals.h"
#include "pin_mux.h"
#include "clock_config.h"

#include "MK64F12.h"

#include "fsl_debug_console.h"
#include "fsl_device_registers.h"
#include "fsl_gpio.h"

#include "lwip/opt.h"
#include "lwip/dhcp.h"
#include "lwip/ip_addr.h"
#include "lwip/prot/dhcp.h"
#include "lwip/tcpip.h"
#include "lwip/api.h"
#include "lwip/sys.h"
#include "ethernetif.h"

#include "lcd.h"
#include "pwm.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/
/* MAC address configuration. */
#define configMAC_ADDR {0x02, 0x12, 0x13, 0x10, 0x15, 0x11}

/*! @brief Stack size of the temporary lwIP initialization thread. */
#define STACK_INIT_THREAD_STACKSIZE 1024

/*******************************************************************************
 * Prototypes
 ******************************************************************************/
static void stack_init_thread(void *);
static void toggle_leds_thread(void *);
void toggle_red(void);
void toggle_green(void);
void toggle_blue(void);

/*******************************************************************************
 * Variables
 ******************************************************************************/

/*******************************************************************************
 * Code
 ******************************************************************************/

/*
 * @brief   Application entry point.
 */
int main(void)
{
    /* Memory protection unit. */
    SYSMPU_Type *base = SYSMPU;

    /* Init board hardware. */
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitBootPeripherals();
    /* Init FSL debug console. */
    BOARD_InitDebugConsole();

    /* Disable SYSMPU. */
    base->CESR &= ~SYSMPU_CESR_VLD_MASK;

    // Inicializa el LCD, indicando la dirección 0x3F para que funcione.
    LCD_Init(0x3F, 16, 2, LCD_5x8DOTS);
    LCD_backlight();
    LCD_clear();

    LCD_setCursor(0, 0);
    LCD_printstr("Obtaining IP");
    LCD_setCursor(0, 1);
    LCD_printstr("Please wait...");

    /* Initialize lwIP from thread */
    if (sys_thread_new("main", stack_init_thread, NULL, STACK_INIT_THREAD_STACKSIZE, DEFAULT_THREAD_PRIO) == NULL)
    {
        LWIP_ASSERT("main(): Stack init thread creation failed.", 0);
    }

    vTaskStartScheduler();

    return 0 ;
}

/*!
 * @brief Initializes lwIP stack.
 */
static void stack_init_thread(void *arg)
{
    static struct netif fsl_netif0;
    struct netif *netif = (struct netif *)&fsl_netif0;
    struct dhcp *dhcp;
    ip4_addr_t fsl_netif0_ipaddr, fsl_netif0_netmask, fsl_netif0_gw;
    ethernetif_config_t fsl_enet_config0 = {
            .phyAddress = BOARD_ENET0_PHY_ADDRESS, .clockName = kCLOCK_CoreSysClk, .macAddress = configMAC_ADDR,
    };

    /* Default IP configuration. */
    IP4_ADDR(&fsl_netif0_ipaddr, 0U, 0U, 0U, 0U);
    IP4_ADDR(&fsl_netif0_netmask, 0U, 0U, 0U, 0U);
    IP4_ADDR(&fsl_netif0_gw, 0U, 0U, 0U, 0U);

    /* Initialization of the IP stack. */
    tcpip_init(NULL, NULL);

    /* Set up the network interface. */
    netif_add(&fsl_netif0, &fsl_netif0_ipaddr, &fsl_netif0_netmask, &fsl_netif0_gw, &fsl_enet_config0, ethernetif0_init,
            tcpip_input);
    netif_set_default(&fsl_netif0);
    netif_set_up(&fsl_netif0);

    /* Query of IP configuration to a local DHCP server. */
    dhcp_start(&fsl_netif0);

    /* Check DHCP state. */
    while (netif_is_up(netif))
    {
        dhcp = netif_dhcp_data(netif);

        if (dhcp != NULL && dhcp->state == DHCP_STATE_BOUND)
        {
            PRINTF("\r\n IPv4 Address     : %s\r\n", ipaddr_ntoa(&netif->ip_addr));
            PRINTF(" IPv4 Subnet mask : %s\r\n",     ipaddr_ntoa(&netif->netmask));
            PRINTF(" IPv4 Gateway     : %s\r\n\r\n", ipaddr_ntoa(&netif->gw));

            /* Initialize toggle_leds_thread */
            if (sys_thread_new("stack_init_thread", toggle_leds_thread, NULL, DEFAULT_THREAD_STACKSIZE, DEFAULT_THREAD_PRIO) == NULL)
            {
                LWIP_ASSERT("stack_init_thread(): toggle leds thread creation failed.", 0);
            }

            LCD_clear();
            LCD_setCursor(0, 0);
            LCD_printstr("IPv4 Address:");
            LCD_setCursor(0, 1);
            LCD_printstr(ipaddr_ntoa(&netif->ip_addr));

            vTaskDelete(NULL);
        }

        sys_msleep(20U);
    }
}

/*!
 * @brief Toggles the LED indicated in a TCP package.
 *
 */
static void toggle_leds_thread(void *arg)
{
    struct netconn *conn, *newconn;
    err_t err, accept_err;
    char buffer[1024];
    struct netbuf *buf;
    void *data;
    u16_t len;
    err_t recv_err;

    /* Create a new TCP connection identifier. */
    conn = netconn_new(NETCONN_TCP);

    if (conn != NULL)
    {
        /* Bind connection to well known port number 1234. */
        err = netconn_bind(conn, NULL, 1234);
        if (err == ERR_OK)
        {
            /* Tell connection to go into listening mode. */
            netconn_listen(conn);

            while (1)
            {
                /* Grab new connection. */
                accept_err = netconn_accept(conn, &newconn);
                /* Process the new connection. */
                if (accept_err == ERR_OK)
                {
                    while (( recv_err = netconn_recv(newconn, &buf)) == ERR_OK)
                    {
                        do
                        {
                            netbuf_copy(buf, buffer, sizeof(buffer));
                            buffer[buf->p->tot_len] = '\0';
                            netbuf_data(buf, &data, &len);

                            if (strcmp(buffer, "red") == 0)
                            {
                                toggle_red();
                            }
                            else if (strcmp(buffer, "green") == 0)
                            {
                                toggle_green();
                            }
                            else if (strcmp(buffer, "blue") == 0)
                            {
                                toggle_blue();
                            }
                            else if (strncmp(buffer, "msg0:", 5) == 0)
                            {
                                char first_row[16];
                                strncpy(first_row, buffer + 5, 16);
                                LCD_clear();
                                LCD_setCursor(0, 0);
                                LCD_printstr(first_row);
                            }
                            else if (strncmp(buffer, "msg1:", 5) == 0)
                            {
                                char second_row[16];
                                strncpy(second_row, buffer + 5, 16);
                                LCD_clear();
                                LCD_setCursor(0, 1);
                                LCD_printstr(second_row);
                            }
                            else if (strncmp(buffer, "pwmw:", 5) == 0)
                            {
                                char number[4];
                                strncpy(number, buffer + 5, 3);
                                number[3] = '\0';
                                long percentage = strtol(number, NULL, 10);
                                update_pwm_dutycyle(WHITE_PWM,  WHITE_CHANNEL,  (uint8_t) percentage);
                            }
                            else if (strncmp(buffer, "pwmg:", 5) == 0)
                            {
                                char number[4];
                                strncpy(number, buffer + 5, 3);
                                number[3] = '\0';
                                long percentage = strtol(number, NULL, 10);
                                update_pwm_dutycyle(GREEN_PWM,  GREEN_CHANNEL,  (uint8_t) percentage);
                            }
                            else if (strncmp(buffer, "pwmy:", 5) == 0)
                            {
                                char number[4];
                                strncpy(number, buffer + 5, 3);
                                number[3] = '\0';
                                long percentage = strtol(number, NULL, 10);
                                update_pwm_dutycyle(YELLOW_PWM, YELLOW_CHANNEL, (uint8_t) percentage);
                            }
                            else if (strncmp(buffer, "pwmr:", 5) == 0)
                            {
                                char number[4];
                                strncpy(number, buffer + 5, 3);
                                number[3] = '\0';
                                long percentage = strtol(number, NULL, 10);
                                update_pwm_dutycyle(RED_PWM,    RED_CHANNEL,     (uint8_t) percentage);
                            }
                        }
                        while (netbuf_next(buf) >= 0);
                        netbuf_delete(buf);
                    }
                    /* Close connection and discard connection identifier. */
                    netconn_close(newconn);
                    netconn_delete(newconn);
                }
            }
        }
        else
        {
            netconn_delete(newconn);
            PRINTF("Can not bind TCP netconn.");
        }
    }
    else
    {
        PRINTF("Can not create TCP netconn.");
    }
}

/*!
 * @brief0 Toggles red LED.
 */
void toggle_red(void)
{
    GPIO_TogglePinsOutput(BOARD_INITLEDS_LED_RED_GPIO, 1 <<  BOARD_INITLEDS_LED_RED_PIN);
}

/*!
 * @brief Toggles green LED.
 */
void toggle_green(void)
{
    GPIO_TogglePinsOutput(BOARD_INITLEDS_LED_GREEN_GPIO, 1 <<  BOARD_INITLEDS_LED_GREEN_PIN);
}

/*!
 * @brief Toggles blue LED.
 */
void toggle_blue(void)
{
    GPIO_TogglePinsOutput(BOARD_INITLEDS_LED_BLUE_GPIO, 1 <<  BOARD_INITLEDS_LED_BLUE_PIN);
}
