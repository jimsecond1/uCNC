/*
    Name: multiboard_protocol.c
    Description: µCNC implementation of a multiboard send-response protocol
    Copyright: Copyright (c) João Martins
    Author: João Martins
    Date: 02/06/2023

    µCNC is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. Please see <http://www.gnu.org/licenses/>

    µCNC is distributed WITHOUT ANY WARRANTY;
    Also without the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
    See the	GNU General Public License for more details.
*/

#include "../cnc.h"

#ifdef ENABLE_MULTIBOARD

slave_board_io_t g_slave_io;
static multiboard_data_t multiboard_data;

#ifndef IS_MASTER_BOARD
// multiboard slave uses the UART port to communicate to master
MCU_RX_CALLBACK void mcu_com_rx_cb(unsigned char c)
#else
void protocol_rx_cb(uint8_t c)
#endif
{
    static int8_t protocol_state = -3;

    int8_t current_state = protocol_state;

    switch (c)
    {
    case MULTIBOARD_PROTOCOL_SOF:
        if (current_state < 0)
        {
            current_state++;
            protocol_state = current_state;
            return;
        }
        break;
    case MULTIBOARD_PROTOCOL_EOF: // received a EOF (eval if is part of the message, it's EOF or error)
        // EOF found at the expected location (cmd + message length + 1 (crc))
        if (multiboard_data.multiboard_frame.length == (current_state - 3))
        {
            multiboard_data.multiboard_frame.crc = multiboard_data.rawdata[(current_state - 1)];
            protocol_state = -3; // reset protocol internal state
            return;
        }
        if (multiboard_data.multiboard_frame.length < current_state)
        {
            protocol_state = -3; // reset protocol internal state
            return;
        }
        break;
    default: // received other character but not yet inside the knowned packet format
        if (current_state < 0)
        {
            protocol_state = -3; // reset protocol internal state
            return;
        }
    }

    // new code arrived without the previous being processed
    // signal error and halt board
    if (multiboard_data.multiboard_frame.crc)
    {
        cnc_alarm(EXEC_ALARM_MULTIBOARD_CRITICAL_ERROR);
    }
    multiboard_data.rawdata[current_state++] = c;
    protocol_state = current_state;
}
#endif

static void multiboard_slave_send_sof(void)
{
    serial_putc(MULTIBOARD_PROTOCOL_SOF); // SOF
    serial_putc(MULTIBOARD_PROTOCOL_SOF); // SOF
    serial_putc(MULTIBOARD_PROTOCOL_SOF); // SOF
}

static void multiboard_slave_send_status_byte(uint16_t word)
{
    // mark command as processed
    multiboard_data.multiboard_frame.crc = 0;
    multiboard_slave_send_sof();
    serial_putc((word >> 8) & 0xFF);      // NACK
    serial_putc(word & 0xFF);             // NACK CRC
    serial_putc(MULTIBOARD_PROTOCOL_EOF); // EOF
}

// all commands are executed in the main loop
void multiboard_slave_dotasks(void)
{
    // there is an available command when the crc is set
    // crc will never be 0
    if (multiboard_data.multiboard_frame.crc)
    {
        // check message CRC
        uint8_t len = multiboard_data.multiboard_frame.length;
        if (len > MULTIBOARD_BUFFER_SIZE)
        {
            multiboard_slave_send_status_byte(MULTIBOARD_PROTOCOL_NACK);
        }
        uint8_t command = multiboard_data.multiboard_frame.command;
        uint8_t crc = crc7(command, 0);
        uint8_t *data = multiboard_data.multiboard_frame.content;
        crc = crc7(len, crc);
        for (uint8_t i = 0; i < len; i++)
        {
            crc = crc7(*data++, crc);
        }

        if (crc != multiboard_data.multiboard_frame.crc)
        {
            multiboard_slave_send_status_byte(MULTIBOARD_PROTOCOL_NACK);
        }

        // run actions
        // switch (command)
        // {
        // case /* constant-expression */:
        //     /* code */
        //     break;

        // default:
        //     // command unknowed. can be use for eventual command extensions
        //     // send ACK the same way
        //     break;
        // }

        multiboard_slave_send_status_byte(MULTIBOARD_PROTOCOL_ACK);
    }

    void multiboard_master_send_command(uint8_t command, uint8_t * data, uint8_t len)
    {
        mcu_uart_putc(MULTIBOARD_PROTOCOL_SOF);
        mcu_uart_putc(MULTIBOARD_PROTOCOL_SOF);
        mcu_uart_putc(MULTIBOARD_PROTOCOL_SOF);
        mcu_uart_putc(command);
        uint8_t crc = crc7(command, 0);
        mcu_uart_putc(len);
        crc = crc7(len, crc);
        for (uint8_t i = 0; i < len; i++)
        {
            mcu_uart_putc(data[i]);
            crc = crc7(data[i], crc);
        }
        mcu_uart_putc(crc);
        mcu_uart_putc(MULTIBOARD_PROTOCOL_EOF);
    }
}