/**
 * This file is part of libsamsung-ipc.
 *
 * Copyright (C) 2011 Paul Kocialkowski <contact@paulk.fr>
 *
 * libsamsung-ipc is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * libsamsung-ipc is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with libsamsung-ipc.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef __RFS_H__
#define __RFS_H__

struct ipc_message_info;

#define IPC_RFS_NV_READ_ITEM                        0x4201
#define IPC_RFS_NV_WRITE_ITEM                       0x4202

struct ipc_rfs_io {
    unsigned int offset;
    unsigned int length;
} __attribute__((__packed__));

struct ipc_rfs_io_confirm {
    unsigned char confirm;
    unsigned int offset;
    unsigned int length;
} __attribute__((__packed__));

#endif

// vim:ts=4:sw=4:expandtab
