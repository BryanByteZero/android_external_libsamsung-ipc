/*
 * This file is part of libsamsung-ipc.
 *
 * Copyright (C) 2010-2011 Joerie de Gram <j.de.gram@gmail.com>
 * Copyright (C) 2011-2013 Paul Kocialkowski <contact@paulk.fr>
 *
 * libsamsung-ipc is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * libsamsung-ipc is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with libsamsung-ipc.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <samsung-ipc.h>

#ifndef __SAMSUNG_IPC_DISP_H__
#define __SAMSUNG_IPC_DISP_H__

/*
 * Commands
 */

#define IPC_DISP_ICON_INFO                                      0x0701
#define IPC_DISP_HOMEZONE_INFO                                  0x0702
#define IPC_DISP_RSSI_INFO                                      0x0706

/*
 * Structures
 */

struct ipc_disp_icon_info {
    unsigned char rssi;
    unsigned char bars;
    unsigned char act;
    unsigned char reg;
} __attribute__((__packed__));

struct ipc_disp_rssi_info {
    unsigned char rssi;
} __attribute__((__packed__));

#endif

// vim:ts=4:sw=4:expandtab
