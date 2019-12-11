/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <linux/major.h>
#include <linux/mmc/ioctl.h>

#include "ipc.h"
#include "log.h"
#include "rpmb.h"
#include "rpmb_protocol.h"
#include "sg.h"
#include "storage.h"

#ifdef RPMB_DEBUG
#include <inttypes.h>
#endif

#define MMC_READ_MULTIPLE_BLOCK 18
#define MMC_WRITE_MULTIPLE_BLOCK 25
#define MMC_RELIABLE_WRITE_FLAG (1 << 31)

#define MMC_RSP_PRESENT (1 << 0)
#define MMC_RSP_CRC (1 << 2)
#define MMC_RSP_OPCODE (1 << 4)
#define MMC_CMD_ADTC (1 << 5)
#define MMC_RSP_SPI_S1 (1 << 7)
#define MMC_RSP_R1 (MMC_RSP_PRESENT | MMC_RSP_CRC | MMC_RSP_OPCODE)
#define MMC_RSP_SPI_R1 (MMC_RSP_SPI_S1)

#define MMC_WRITE_FLAG_R 0
#define MMC_WRITE_FLAG_W 1
#define MMC_WRITE_FLAG_RELW (MMC_WRITE_FLAG_W | MMC_RELIABLE_WRITE_FLAG)

#define MMC_BLOCK_SIZE 512

/* CDB format of SECURITY PROTOCOL IN/OUT commands */
struct sec_proto_cdb {
    /*
     * OPERATION CODE = A2h for SECURITY PROTOCOL IN command,
     * OPERATION CODE = B5h for SECURITY PROTOCOL OUT command.
     */
    uint8_t opcode;
    /* SECURITY PROTOCOL = ECh (JEDEC Universal Flash Storage) */
    uint8_t sec_proto;
    /*
     * The SECURITY PROTOCOL SPECIFIC field specifies the RPMB Protocol ID.
     * CDB Byte 2 = 00h and CDB Byte 3 = 01h for RPMB Region 0.
     */
    uint8_t cdb_byte_2;
    uint8_t cdb_byte_3;
    /*
     * Byte 4 and 5 are reserved.
     */
    uint8_t cdb_byte_4;
    uint8_t cdb_byte_5;
    /* ALLOCATION/TRANSFER LENGTH in big-endian */
    uint32_t length;
    /* Byte 9 is reserved. */
    uint8_t cdb_byte_10;
    /* CONTROL = 00h. */
    uint8_t ctrl;
} __packed;

static int rpmb_fd = -1;
static uint8_t read_buf[4096];
static enum dev_type dev_type = UNKNOWN_RPMB;

#ifdef RPMB_DEBUG

static void print_buf(const char* prefix, const uint8_t* buf, size_t size) {
    size_t i;

    printf("%s @%p [%zu]", prefix, buf, size);
    for (i = 0; i < size; i++) {
        if (i && i % 32 == 0) printf("\n%*s", (int)strlen(prefix), "");
        printf(" %02x", buf[i]);
    }
    printf("\n");
    fflush(stdout);
}

#endif

static int send_mmc_rpmb_req(int mmc_fd, const struct storage_rpmb_send_req* req) {
    struct {
        struct mmc_ioc_multi_cmd multi;
        struct mmc_ioc_cmd cmd_buf[3];
    } mmc = {};
    struct mmc_ioc_cmd* cmd = mmc.multi.cmds;
    int rc;

    const uint8_t* write_buf = req->payload;
    if (req->reliable_write_size) {
        cmd->write_flag = MMC_WRITE_FLAG_RELW;
        cmd->opcode = MMC_WRITE_MULTIPLE_BLOCK;
        cmd->flags = MMC_RSP_SPI_R1 | MMC_RSP_R1 | MMC_CMD_ADTC;
        cmd->blksz = MMC_BLOCK_SIZE;
        cmd->blocks = req->reliable_write_size / MMC_BLOCK_SIZE;
        mmc_ioc_cmd_set_data((*cmd), write_buf);
#ifdef RPMB_DEBUG
        ALOGI("opcode: 0x%x, write_flag: 0x%x\n", cmd->opcode, cmd->write_flag);
        print_buf("request: ", write_buf, req->reliable_write_size);
#endif
        write_buf += req->reliable_write_size;
        mmc.multi.num_of_cmds++;
        cmd++;
    }

    if (req->write_size) {
        cmd->write_flag = MMC_WRITE_FLAG_W;
        cmd->opcode = MMC_WRITE_MULTIPLE_BLOCK;
        cmd->flags = MMC_RSP_SPI_R1 | MMC_RSP_R1 | MMC_CMD_ADTC;
        cmd->blksz = MMC_BLOCK_SIZE;
        cmd->blocks = req->write_size / MMC_BLOCK_SIZE;
        mmc_ioc_cmd_set_data((*cmd), write_buf);
#ifdef RPMB_DEBUG
        ALOGI("opcode: 0x%x, write_flag: 0x%x\n", cmd->opcode, cmd->write_flag);
        print_buf("request: ", write_buf, req->write_size);
#endif
        write_buf += req->write_size;
        mmc.multi.num_of_cmds++;
        cmd++;
    }

    if (req->read_size) {
        cmd->write_flag = MMC_WRITE_FLAG_R;
        cmd->opcode = MMC_READ_MULTIPLE_BLOCK;
        cmd->flags = MMC_RSP_SPI_R1 | MMC_RSP_R1 | MMC_CMD_ADTC, cmd->blksz = MMC_BLOCK_SIZE;
        cmd->blocks = req->read_size / MMC_BLOCK_SIZE;
        mmc_ioc_cmd_set_data((*cmd), read_buf);
#ifdef RPMB_DEBUG
        ALOGI("opcode: 0x%x, write_flag: 0x%x\n", cmd->opcode, cmd->write_flag);
#endif
        mmc.multi.num_of_cmds++;
        cmd++;
    }

    rc = ioctl(mmc_fd, MMC_IOC_MULTI_CMD, &mmc.multi);
    if (rc < 0) {
        ALOGE("%s: mmc ioctl failed: %d, %s\n", __func__, rc, strerror(errno));
    }
    return rc;
}

static int send_ufs_rpmb_req(int sg_fd, const struct storage_rpmb_send_req* req) {
    int rc;
    const uint8_t* write_buf = req->payload;
    struct sec_proto_cdb in_cdb = {0xA2, 0xEC, 0x00, 0x01, 0x00, 0x00, 0, 0x00, 0x00};
    struct sec_proto_cdb out_cdb = {0xB5, 0xEC, 0x00, 0x01, 0x00, 0x00, 0, 0x00, 0x00};
    unsigned char sense_buffer[32];

    if (req->reliable_write_size) {
#ifdef RPMB_DEBUG
        ALOGW("-------------------------Begining reliable_write_size\n");
        ALOGW("reliable_write_size: %d", (int)req->reliable_write_size);
        struct rpmb_packet* pp = (struct rpmb_packet*)write_buf;
        ALOGW("write_counter: %" PRIu32, rpmb_get_u32(pp->write_counter));
        ALOGW("address: %" PRIu16, rpmb_get_u16(pp->address));
        ALOGW("block_count: %" PRIu16, rpmb_get_u16(pp->block_count));
        ALOGW("result: %" PRIu16, rpmb_get_u16(pp->result));
        ALOGW("req_resp: %" PRIu16, rpmb_get_u16(pp->req_resp));
#endif
        /* Prepare SECURITY PROTOCOL OUT command. */
        out_cdb.length = __builtin_bswap32(req->reliable_write_size);
        sg_io_hdr_t io_hdr;
        memset(&io_hdr, 0, sizeof(sg_io_hdr_t));
        io_hdr.interface_id = 'S';
        io_hdr.dxfer_direction = SG_DXFER_TO_DEV;
        io_hdr.cmd_len = sizeof(out_cdb);
        io_hdr.mx_sb_len = sizeof(sense_buffer);
        io_hdr.dxfer_len = req->reliable_write_size;
        io_hdr.dxferp = (void*)write_buf;
        io_hdr.cmdp = (unsigned char*)&out_cdb;
        io_hdr.sbp = sense_buffer;
        /* 20000 millisecs == 20 seconds */
        io_hdr.timeout = 20000;
        rc = ioctl(sg_fd, SG_IO, &io_hdr);
        if (rc < 0) {
            ALOGE("%s: ufs ioctl failed: %d, %s\n", __func__, rc, strerror(errno));
        }
        write_buf += req->reliable_write_size;
#ifdef RPMB_DEBUG
        ALOGW("-------------------------End of reliable_write_size\n");
#endif
    }

    if (req->write_size) {
        struct rpmb_packet* pp = (struct rpmb_packet*)write_buf;
        if (rpmb_get_u16(pp->req_resp) == RPMB_REQ_DATA_READ) {
            pp->block_count = rpmb_u16(req->read_size / 512);
        }
#ifdef RPMB_DEBUG
        ALOGW("-------------------------Begining write_size\n");
        ALOGW("write_size: %d", (int)req->write_size);
        ALOGW("write_counter: %" PRIu32, rpmb_get_u32(pp->write_counter));
        ALOGW("address: %" PRIu16, rpmb_get_u16(pp->address));
        ALOGW("block_count: %" PRIu16, rpmb_get_u16(pp->block_count));
        ALOGW("result: %" PRIu16, rpmb_get_u16(pp->result));
        ALOGW("req_resp: %" PRIu16, rpmb_get_u16(pp->req_resp));
        ALOGW("block_count: %" PRIu16, rpmb_get_u16(pp->block_count));
#endif
        /* Prepare SECURITY PROTOCOL OUT command. */
        out_cdb.length = __builtin_bswap32(req->write_size);
        sg_io_hdr_t io_hdr;
        memset(&io_hdr, 0, sizeof(sg_io_hdr_t));
        io_hdr.interface_id = 'S';
        io_hdr.dxfer_direction = SG_DXFER_TO_DEV;
        io_hdr.cmd_len = sizeof(out_cdb);
        io_hdr.mx_sb_len = sizeof(sense_buffer);
        io_hdr.dxfer_len = req->write_size;
        io_hdr.dxferp = (void*)write_buf;
        io_hdr.cmdp = (unsigned char*)&out_cdb;
        io_hdr.sbp = sense_buffer;
        /* 20000 millisecs == 20 seconds */
        io_hdr.timeout = 20000;
        rc = ioctl(sg_fd, SG_IO, &io_hdr);
        if (rc < 0) {
            ALOGE("%s: ufs ioctl failed: %d, %s\n", __func__, rc, strerror(errno));
        }
        write_buf += req->write_size;
#ifdef RPMB_DEBUG
        ALOGW("-------------------------End of write_size\n");
#endif
    }

    if (req->read_size) {
#ifdef RPMB_DEBUG
        ALOGW("-------------------------Begining read_size\n");
        ALOGW("read_size: %d", (int)req->read_size);
#endif
        /* Prepare SECURITY PROTOCOL IN command. */
        out_cdb.length = __builtin_bswap32(req->read_size);
        sg_io_hdr_t io_hdr;
        memset(&io_hdr, 0, sizeof(sg_io_hdr_t));
        io_hdr.interface_id = 'S';
        io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
        io_hdr.cmd_len = sizeof(in_cdb);
        io_hdr.mx_sb_len = sizeof(sense_buffer);
        /* TODO: figure out the difference between "data transfer associated
         * with the command" and "ALLOCATION/TRANSFER LENGTH". */
        io_hdr.dxfer_len = req->read_size;
        io_hdr.dxferp = read_buf;
        io_hdr.cmdp = (unsigned char*)&in_cdb;
        io_hdr.sbp = sense_buffer;
        /* 20000 millisecs == 20 seconds */
        io_hdr.timeout = 20000;
        rc = ioctl(sg_fd, SG_IO, &io_hdr);
        if (rc < 0) {
            ALOGE("%s: ufs ioctl failed: %d, %s\n", __func__, rc, strerror(errno));
        }
#ifdef RPMB_DEBUG
        struct rpmb_packet* pp = (struct rpmb_packet*)read_buf;
        ALOGW("write_counter: %" PRIu32, rpmb_get_u32(pp->write_counter));
        ALOGW("address: %" PRIu16, rpmb_get_u16(pp->address));
        ALOGW("block_count: %" PRIu16, rpmb_get_u16(pp->block_count));
        ALOGW("result: %" PRIu16, rpmb_get_u16(pp->result));
        ALOGW("req_resp: %" PRIu16, rpmb_get_u16(pp->req_resp));
        ALOGW("-------------------------End of read_size\n");
#endif
    }
    return rc;
}

static int send_virt_rpmb_req(int rpmb_fd, void* read_buf, size_t read_size, const void* payload,
                              size_t payload_size) {
    int rc;
    uint16_t res_count = read_size / MMC_BLOCK_SIZE;
    uint16_t cmd_count = payload_size / MMC_BLOCK_SIZE;
    rc = write(rpmb_fd, &res_count, sizeof(res_count));
    if (rc < 0) {
        return rc;
    }
    rc = write(rpmb_fd, &cmd_count, sizeof(cmd_count));
    if (rc < 0) {
        return rc;
    }
    rc = write(rpmb_fd, payload, payload_size);
    if (rc < 0) {
        return rc;
    }
    rc = read(rpmb_fd, read_buf, read_size);
    return rc;
}

int rpmb_send(struct storage_msg* msg, const void* r, size_t req_len) {
    int rc;
    const struct storage_rpmb_send_req* req = r;

    if (req_len < sizeof(*req)) {
        ALOGW("malformed rpmb request: invalid length (%zu < %zu)\n", req_len, sizeof(*req));
        msg->result = STORAGE_ERR_NOT_VALID;
        goto err_response;
    }

    size_t expected_len = sizeof(*req) + req->reliable_write_size + req->write_size;
    if (req_len != expected_len) {
        ALOGW("malformed rpmb request: invalid length (%zu != %zu)\n", req_len, expected_len);
        msg->result = STORAGE_ERR_NOT_VALID;
        goto err_response;
    }

    if ((req->reliable_write_size % MMC_BLOCK_SIZE) != 0) {
        ALOGW("invalid reliable write size %u\n", req->reliable_write_size);
        msg->result = STORAGE_ERR_NOT_VALID;
        goto err_response;
    }

    if ((req->write_size % MMC_BLOCK_SIZE) != 0) {
        ALOGW("invalid write size %u\n", req->write_size);
        msg->result = STORAGE_ERR_NOT_VALID;
        goto err_response;
    }

    if (req->read_size % MMC_BLOCK_SIZE != 0 || req->read_size > sizeof(read_buf)) {
        ALOGE("%s: invalid read size %u\n", __func__, req->read_size);
        msg->result = STORAGE_ERR_NOT_VALID;
        goto err_response;
    }

    if (dev_type == MMC_RPMB) {
        rc = send_mmc_rpmb_req(rpmb_fd, req);
        if (rc < 0) {
            msg->result = STORAGE_ERR_GENERIC;
            goto err_response;
        }
    } else if (dev_type == UFS_RPMB) {
        rc = send_ufs_rpmb_req(rpmb_fd, req);
        if (rc < 0) {
            ALOGE("send_ufs_rpmb_req failed: %d, %s\n", rc, strerror(errno));
            msg->result = STORAGE_ERR_GENERIC;
            goto err_response;
        }
    } else if ((dev_type == VIRT_RPMB) || (dev_type == SOCK_RPMB)) {
        size_t payload_size = req->reliable_write_size + req->write_size;
        rc = send_virt_rpmb_req(rpmb_fd, read_buf, req->read_size, req->payload, payload_size);
        if (rc < 0) {
            ALOGE("send_virt_rpmb_req failed: %d, %s\n", rc, strerror(errno));
            msg->result = STORAGE_ERR_GENERIC;
            goto err_response;
        }
        if (rc != req->read_size) {
            ALOGE("send_virt_rpmb_req got incomplete response: "
                  "(size %d, expected %d)\n",
                  rc, req->read_size);
            msg->result = STORAGE_ERR_GENERIC;
            goto err_response;
        }
    } else {
        ALOGE("Unsupported dev_type\n");
        msg->result = STORAGE_ERR_GENERIC;
        goto err_response;
    }
#ifdef RPMB_DEBUG
    if (req->read_size) print_buf("response: ", read_buf, req->read_size);
#endif

    if (msg->flags & STORAGE_MSG_FLAG_POST_COMMIT) {
        /*
         * Nothing todo for post msg commit request as MMC_IOC_MULTI_CMD
         * is fully synchronous in this implementation.
         */
    }

    msg->result = STORAGE_NO_ERROR;
    return ipc_respond(msg, read_buf, req->read_size);

err_response:
    return ipc_respond(msg, NULL, 0);
}

int rpmb_open(const char* rpmb_devname, enum dev_type open_dev_type) {
    int rc, k;
    dev_type = open_dev_type;

    if (dev_type != SOCK_RPMB) {
        rc = open(rpmb_devname, O_RDWR, 0);
        if (rc < 0) {
            ALOGE("unable (%d) to open rpmb device '%s': %s\n", errno, rpmb_devname, strerror(errno));
            return rc;
        }
        rpmb_fd = rc;
    } else {
        struct sockaddr_un unaddr;
        struct sockaddr *addr = (struct sockaddr *)&unaddr;
        rc = socket(AF_UNIX, SOCK_STREAM, 0);
        if (rc < 0) {
            ALOGE("unable (%d) to create socket: %s\n", errno, strerror(errno));
            return rc;
        }
        rpmb_fd = rc;

        memset(&unaddr, 0, sizeof(unaddr));
        unaddr.sun_family = AF_UNIX;
        // TODO if it overflowed, bail rather than connecting?
        strncpy(unaddr.sun_path, rpmb_devname, sizeof(unaddr.sun_path)-1);
        rc = connect(rpmb_fd, addr, sizeof(unaddr));
        if (rc < 0) {
            ALOGE("unable (%d) to connect to rpmb socket '%s': %s\n", errno, rpmb_devname, strerror(errno));
            return rc;
        }
    }

    /* For UFS, it is prudent to check we hava a sg device by calling an ioctl */
    if (dev_type == UFS_RPMB) {
        if ((ioctl(rc, SG_GET_VERSION_NUM, &k) < 0) || (k < 30000)) {
            ALOGE("%s is not a sg device, or old sg driver\n", rpmb_devname);
            return -1;
        }
    }
    rpmb_fd = rc;
    return 0;
}

void rpmb_close(void) {
    close(rpmb_fd);
    rpmb_fd = -1;
}
