/**
 * This file is part of libsamsung-ipc.
 *
 * Copyright (C) 2011 Paul Kocialkowski <contact@paulk.fr>
 *                    Joerie de Gram <j.de.gram@gmail.com>
 *                    Simon Busch <morphis@gravedo.de>
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
 */

#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <stdbool.h>
#include <termios.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <asm/types.h>
#include <mtd/mtd-abi.h>
#include <assert.h>

#include <radio.h>

#include "galaxysmtd_modem_ctl.h"
#include "galaxysmtd_nv_data.h"
#include "galaxysmtd_ipc.h"
#include "ipc_private.h"

int wake_lock_fd=   -1;
int wake_unlock_fd= -1;

int galaxysmtd_modem_bootstrap(struct ipc_client *client)
{
    int s3c2410_serial3_fd= -1;
    int modem_ctl_fd=   -1;

    /* Control variables. */
    int boot_tries_count=0;
    int rc=0;

    /* Boot variables */
    uint8_t *radio_img_p=NULL;
    uint8_t bootcore_version=0;
    uint8_t info_size=0;
    uint8_t crc_byte=0;
    int block_size=0;

    /* s3c2410 serial setup variables. */
    struct termios termios;
    int serial;

    /* fds maniplation variables */
    struct timeval timeout;
    fd_set fds;

    /* nv_data variables */
    void *nv_data_p;

    /* General purpose variables. */
    uint8_t data;
    uint16_t data_16;
    uint8_t *data_p;
    unsigned int modem_ctl_data;
    int i;
    void *onedram_map=NULL;

    ipc_client_log(client, "galaxysmtd_ipc_bootstrap: enter");

boot_loop_start:
    if(boot_tries_count > 5)
    {
        ipc_client_log(client, "galaxysmtd_ipc_bootstrap: boot has failed too many times.");
        goto error;
    }

    //TODO try to disable svnet0 first?

    /* Read the radio.img image. */
    ipc_client_log(client, "galaxysmtd_ipc_bootstrap: reading radio image");
    radio_img_p = ipc_mtd_read(client, "/dev/block/bml12", RADIO_IMG_SIZE, 0x1000);
    if (radio_img_p == NULL) {
        goto error;
    }
    ipc_client_log(client, "galaxysmtd_ipc_bootstrap: radio image read");

    ipc_client_log(client, "galaxysmtd_ipc_bootstrap: open modem_ctl");
    modem_ctl_fd = open("/dev/onedram", O_RDWR);
    if(modem_ctl_fd < 0)
        goto error_loop;

    /* Reset the modem before init to send the first part of modem.bin. */
    ipc_client_power_off(client);
    ipc_client_log(client, "galaxysmtd_ipc_bootstrap: sent PHONE \"off\" command");
    usleep(1000);
    ipc_client_power_on(client);
    ipc_client_log(client, "galaxysmtd_ipc_bootstrap: sent PHONE \"on\" command");
    usleep(200000);

    ipc_client_log(client, "galaxysmtd_ipc_bootstrap: open s3c2410_serial3");
    s3c2410_serial3_fd = open("/dev/s3c2410_serial3", O_RDWR);
    if(s3c2410_serial3_fd < 0)
        goto error_loop;

    /* Setup the s3c2410 serial. */
    ipc_client_log(client, "galaxysmtd_ipc_bootstrap: setup s3c2410_serial3");

    ioctl(s3c2410_serial3_fd, TIOCMGET, &serial); //FIXME
    ioctl(s3c2410_serial3_fd, TIOCMSET, &serial); //FIXME

    tcgetattr(s3c2410_serial3_fd, &termios); //FIXME
    tcsetattr(s3c2410_serial3_fd, TCSANOW, &termios); //FIXME

    tcgetattr(s3c2410_serial3_fd, &termios);

    cfmakeraw(&termios);
    cfsetispeed(&termios, B115200);
    cfsetospeed(&termios, B115200);

    tcsetattr(s3c2410_serial3_fd, TCSANOW, &termios);

    /* Send 'AT' in ASCII. */
    ipc_client_log(client, "galaxysmtd_ipc_bootstrap: sending AT in ASCII");
    for(i=0 ; i < 20 ; i++)
    {
        rc = write(s3c2410_serial3_fd, "AT", 2);
        usleep(50000);
    }
    ipc_client_log(client, "galaxysmtd_ipc_bootstrap: sending AT in ASCII done");

    usleep(50000); //FIXME

    /* Get and check bootcore version. */
    read(s3c2410_serial3_fd, &bootcore_version, sizeof(bootcore_version));
    ipc_client_log(client, "galaxysmtd_ipc_bootstrap: got bootcore version: 0x%x", bootcore_version);

    if(bootcore_version != BOOTCORE_VERSION)
        goto error_loop;

    /* Get info_size. */
    read(s3c2410_serial3_fd, &info_size, sizeof(info_size));
    ipc_client_log(client, "galaxysmtd_ipc_bootstrap: got info_size: 0x%x", info_size);

    /* Send PSI magic. */
    data=PSI_MAGIC;
    write(s3c2410_serial3_fd, &data, sizeof(data));
    ipc_client_log(client, "galaxysmtd_ipc_bootstrap: sent PSI_MAGIC (0x%x)", PSI_MAGIC);

    /* Send PSI data len. */
    data_16=PSI_DATA_LEN;
    data_p=(uint8_t *)&data_16;

    for(i=0 ; i < 2 ; i++)
    {
        write(s3c2410_serial3_fd, data_p, 1);
        data_p++;
    }
    ipc_client_log(client, "galaxysmtd_ipc_bootstrap: sent PSI_DATA_LEN (0x%x)", PSI_DATA_LEN);

    /* Write the first part of modem.img. */
    FD_ZERO(&fds);
    FD_SET(s3c2410_serial3_fd, &fds);

    timeout.tv_sec=5;
    timeout.tv_usec=0;

    data_p=radio_img_p;

    ipc_client_log(client, "galaxysmtd_ipc_bootstrap: sending the first part of modem.bin");

    crc_byte=0;
    for(i=0 ; i < PSI_DATA_LEN ; i++)
    {
        if(select(FD_SETSIZE, NULL, &fds, NULL, &timeout) == 0)
        {
            ipc_client_log(client, "galaxysmtd_ipc_bootstrap: select timeout passed");
            goto error_loop;
        }

        write(s3c2410_serial3_fd, data_p, 1);
        crc_byte=crc_byte ^ *data_p;

        data_p++;
    }

    ipc_client_log(client, "galaxysmtd_ipc_bootstrap: first part of modem.bin sent; crc_byte is 0x%x", crc_byte);

    if(select(FD_SETSIZE, NULL, &fds, NULL, &timeout) == 0)
    {
        ipc_client_log(client, "galaxysmtd_ipc_bootstrap: select timeout passed");
        goto error_loop;
    }

    write(s3c2410_serial3_fd, &crc_byte, sizeof(crc_byte));

    ipc_client_log(client, "galaxysmtd_ipc_bootstrap: crc_byte sent");

    ipc_client_log(client, "galaxysmtd_ipc_bootstrap: wait for ACK");
    data = 0;
    for(i = 0 ; data != 0x01 ; i++)
    {
        if(select(FD_SETSIZE, &fds, NULL, NULL, &timeout) == 0)
        {
            ipc_client_log(client, "galaxysmtd_ipc_bootstrap: select timeout passed");
            goto error_loop;
        }

        read(s3c2410_serial3_fd, &data, sizeof(data));

        if(i > 50)
        {
            ipc_client_log(client, "galaxysmtd_ipc_bootstrap: fairly too much attempts to get ACK");
            goto error_loop;
        }
    }
    ipc_client_log(client, "galaxysmtd_ipc_bootstrap: got ACK");

    ipc_client_log(client, "galaxysmtd_ipc_bootstrap: close s3c2410_serial3");
    close(s3c2410_serial3_fd);

    FD_ZERO(&fds);
    FD_SET(modem_ctl_fd, &fds);

    ipc_client_log(client, "galaxysmtd_ipc_bootstrap: wait for 0x12341234");
    if(select(modem_ctl_fd+1, &fds, NULL, NULL, &timeout) == 0)
    {
        ipc_client_log(client, "galaxysmtd_ipc_bootstrap: select timeout passed");
        goto error_loop;
    }

    read(modem_ctl_fd, &modem_ctl_data, sizeof(modem_ctl_data));
    if(modem_ctl_data != 0x12341234) {
        ipc_client_log(client, "galaxysmtd_ipc_bootstrap: wrong ACK flag from modem_ctl");
        goto error_loop;
    }
    ipc_client_log(client, "galaxysmtd_ipc_bootstrap: got 0x12341234");

    ipc_client_log(client, "galaxysmtd_ipc_bootstrap: writing the rest of modem.bin to modem_ctl.");

    /* Pointer to the remaining part of radio.img. */
    data_p=radio_img_p + PSI_DATA_LEN;

    size_t map_length = 16773120; //magic buflen comes from proprietary ril trace
    onedram_map = mmap(NULL, map_length, PROT_READ|PROT_WRITE, MAP_SHARED, modem_ctl_fd, 0);
    if(onedram_map == -1) {
        ipc_client_log(client, "galaxysmtd_ipc_bootstrap: could not map onedram to memory");
        goto error_loop;
    }

    memcpy(onedram_map, data_p, RADIO_IMG_SIZE - PSI_DATA_LEN);

    free(radio_img_p);

    /* nv_data part. */

    /* Check if all the nv_data files are ok. */
    nv_data_check(client);

    /* Check if the MD5 is ok. */
    nv_data_md5_check(client);

    /* Write nv_data.bin to modem_ctl. */
    ipc_client_log(client, "galaxysmtd_ipc_bootstrap: write nv_data to modem_ctl");

    nv_data_p = ipc_file_read(client, "/efs/nv_data.bin", NV_DATA_SIZE, 1024);
    if (nv_data_p == NULL)
        goto error;
    data_p = nv_data_p;

    //seems 0xd80000 is the absolute maximum for the radio_img, like crespo
    //uses. it creates a gap though, since we only write 0x9fb000 bytes to /dev/onedram
    memcpy(onedram_map + 0xd80000, data_p, NV_DATA_SIZE);

    ipc_client_log(client, "galaxysmtd_ipc_bootstrap: done writing nv_data");

    free(nv_data_p);
    munmap(onedram_map, map_length);

    //TODO finalize bootstrap
    ioctl(modem_ctl_fd, 0x6f22, 0); //magic request comes from proprietary ril trace

    ipc_client_log(client, "galaxysmtd_ipc_bootstrap: send 0x45674567");
    modem_ctl_data = 0x45674567;
    write(modem_ctl_fd, &modem_ctl_data, sizeof(modem_ctl_data));

    ipc_client_log(client, "galaxysmtd_ipc_bootstrap: wait for 0xabcdabcd");
    for(i = 0 ; modem_ctl_data != 0xabcdabcd ; i++)
    {
        if(select(FD_SETSIZE, &fds, NULL, NULL, &timeout) == 0)
        {
            ipc_client_log(client, "galaxysmtd_ipc_bootstrap: select timeout passed");
            goto error_loop;
        }

        read(modem_ctl_fd, &modem_ctl_data, sizeof(modem_ctl_data));

        if(i > 10)
        {
            ipc_client_log(client, "galaxysmtd_ipc_bootstrap: fairly too much attempts to get 0xabcdabcd");
            goto error_loop;
        }
    }
    ipc_client_log(client, "galaxysmtd_ipc_bootstrap: got 0xabcdabcd");

    close(modem_ctl_fd);

    rc = 0;
    goto exit;

error_loop:
    ipc_client_log(client, "%s: something went wrong", __func__);
    boot_tries_count++;
    sleep(2);

    goto boot_loop_start;

error:
    ipc_client_log(client, "%s: something went wrong", __func__);
    rc = 1;
exit:
    ipc_client_log(client, "galaxysmtd_ipc_bootstrap: exit");
    return rc;
}

int crespo_ipc_client_send(struct ipc_client *client, struct ipc_message_info *request)
{
    struct modem_io modem_data;
    struct ipc_header reqhdr;
    int rc = 0;

    memset(&modem_data, 0, sizeof(struct modem_io));
    modem_data.size = request->length + sizeof(struct ipc_header);

    reqhdr.mseq = request->mseq;
    reqhdr.aseq = request->aseq;
    reqhdr.group = request->group;
    reqhdr.index = request->index;
    reqhdr.type = request->type;
    reqhdr.length = (uint16_t) (request->length + sizeof(struct ipc_header));

    modem_data.data = malloc(reqhdr.length);

    memcpy(modem_data.data, &reqhdr, sizeof(struct ipc_header));
    memcpy((unsigned char *) (modem_data.data + sizeof(struct ipc_header)), request->data, request->length);

    assert(client->handlers->write != NULL);

    ipc_client_log(client, "INFO: crespo_ipc_client_send: Modem SEND FMT (id=%d cmd=%d size=%d)!", modem_data.id, modem_data.cmd, modem_data.size);
    ipc_client_log(client, "INFO: crespo_ipc_client_send: request: type = %d (%s), group = %d, index = %d (%s)",
                   request->type, ipc_request_type_to_str(request->type), request->group, request->index, ipc_command_type_to_str(IPC_COMMAND(request)));

#ifdef DEBUG
    if(request->length > 0)
    {
        ipc_client_log(client, "INFO: ==== DATA DUMP ====");
        ipc_hex_dump(client, (void *) request->data, request->length);
    }
#endif

    ipc_client_log(client, "");

    rc = client->handlers->write((uint8_t*) &modem_data, sizeof(struct modem_io), client->handlers->write_data);
    return rc;
}

int wake_lock(char *lock_name, int len)
{
    int rc = 0;

    wake_lock_fd = open("/sys/power/wake_lock", O_RDWR);
    rc = write(wake_lock_fd, lock_name, len);
    close(wake_lock_fd);

    return rc;
}

int wake_unlock(char *lock_name, int len)
{
    int rc = 0;

    wake_lock_fd = open("/sys/power/wake_unlock", O_RDWR);
    rc = write(wake_lock_fd, lock_name, len);
    close(wake_unlock_fd);

    return rc;
}

int crespo_ipc_client_recv(struct ipc_client *client, struct ipc_message_info *response)
{
    struct modem_io modem_data;
    struct ipc_header *resphdr;
    int bread = 0;

    memset(&modem_data, 0, sizeof(struct modem_io));
    modem_data.data = malloc(MAX_MODEM_DATA_SIZE);
    modem_data.size = MAX_MODEM_DATA_SIZE;

    memset(response, 0, sizeof(struct ipc_message_info));

    wake_lock("secril_fmt-interface", 20);

    assert(client->handlers->read != NULL);
    bread = client->handlers->read((uint8_t*) &modem_data, sizeof(struct modem_io) + MAX_MODEM_DATA_SIZE, client->handlers->read_data);
    if (bread < 0)
    {
        ipc_client_log(client, "ERROR: crespo_ipc_client_recv: can't receive enough bytes from modem to process incoming response!");
        return 1;
    }

    ipc_client_log(client, "INFO: crespo_ipc_client_recv: Modem RECV FMT (id=%d cmd=%d size=%d)!", modem_data.id, modem_data.cmd, modem_data.size);

    if(modem_data.size <= 0 || modem_data.size >= 0x1000 || modem_data.data == NULL)
    {
        ipc_client_log(client, "ERROR: crespo_ipc_client_recv: we retrieve less bytes from the modem than we exepected!");
        return 1;
    }

    /* You MUST send back  modem_data */

    resphdr = (struct ipc_header *) modem_data.data;

    response->mseq = resphdr->mseq;
    response->aseq = resphdr->aseq;
    response->group = resphdr->group;
    response->index = resphdr->index;
    response->type = resphdr->type;
    response->length = modem_data.size - sizeof(struct ipc_header);
    response->data = NULL;

    ipc_client_log(client, "INFO: crespo_ipc_client_recv: response: type = %d (%s), group = %d, index = %d (%s)",
                   resphdr->type, ipc_response_type_to_str(resphdr->type), resphdr->group, resphdr->index, ipc_command_type_to_str(IPC_COMMAND(resphdr)));

    if(response->length > 0)
    {
#ifdef DEBUG
        ipc_client_log(client, "INFO: ==== DATA DUMP ====");
        ipc_hex_dump(client, (void *) (modem_data.data + sizeof(struct ipc_header)), response->length);
#endif
        response->data = malloc(response->length);
        memcpy(response->data, (uint8_t *) modem_data.data + sizeof(struct ipc_header), response->length);
    }

    free(modem_data.data);

    ipc_client_log(client, "");

    wake_unlock("secril_fmt-interface", 20);

    return 0;
}

int crespo_ipc_open(void *data, unsigned int size, void *io_data)
{
    int type = *((int *) data);
    int fd = -1;

    switch(type)
    {
        case IPC_CLIENT_TYPE_FMT:
            fd = open("/dev/modem_fmt", O_RDWR | O_NOCTTY | O_NONBLOCK);
            break;
        case IPC_CLIENT_TYPE_RFS:
            fd = open("/dev/modem_rfs", O_RDWR | O_NOCTTY | O_NONBLOCK);
            break;
        default:
            break;
    }

    if(fd < 0)
        return -1;

    if(io_data == NULL)
        return -1;

    memcpy(io_data, &fd, sizeof(int));

    return 0;
}

int crespo_ipc_close(void *data, unsigned int size, void *io_data)
{
    int fd = -1;

    if(io_data == NULL)
        return -1;

    fd = *((int *) io_data);

    if(fd < 0)
        return -1;

    close(fd);

    return 0;
}

int crespo_ipc_read(void *data, unsigned int size, void *io_data)
{
    int fd = -1;
    int rc;

    if(io_data == NULL)
        return -1;

    if(data == NULL)
        return -1;

    fd = *((int *) io_data);

    if(fd < 0)
        return -1;

    rc = ioctl(fd, IOCTL_MODEM_RECV, data);

    if(rc < 0)
        return -1;

    return 0;
}

int crespo_ipc_write(void *data, unsigned int size, void *io_data)
{
    int fd = -1;
    int rc;

    if(io_data == NULL)
        return -1;

    fd = *((int *) io_data);

    if(fd < 0)
        return -1;

    rc = ioctl(fd, IOCTL_MODEM_SEND, data);

    if(rc < 0)
        return -1;

    return 0;
}

int galaxysmtd_ipc_power_on(void *data)
{
    int fd = open("/sys/class/modemctl/xmm/control", O_RDWR);
    int rc;

    if(fd < 0)
        return -1;

    rc = write(fd, "on", 2);
    close(fd);

    if(rc < 0)
        return -1;

    //TODO should check if nothing was written as well?

    return 0;
}

int galaxysmtd_ipc_power_off(void *data)
{
    int fd = open("/sys/class/modemctl/xmm/control", O_RDWR);
    int rc;

    if(fd < 0)
        return -1;

    rc = write(fd, "off", 3);
    close(fd);

    if(rc < 0)
        return -1;

    //TODO should check if nothing was written as well?

    return 0;
}

struct ipc_handlers ipc_default_handlers = {
    .read = crespo_ipc_read,
    .write = crespo_ipc_write,
    .open = crespo_ipc_open,
    .close = crespo_ipc_close,
    .power_on = galaxysmtd_ipc_power_on,
    .power_off = galaxysmtd_ipc_power_off,
};

struct ipc_ops ipc_ops = {
    .send = crespo_ipc_client_send,
    .recv = crespo_ipc_client_recv,
    .bootstrap = galaxysmtd_modem_bootstrap,
};

// vim:ts=4:sw=4:expandtab
