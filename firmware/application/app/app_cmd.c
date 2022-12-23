#include "fds_util.h"
#include "bsp_time.h"
#include "bsp_delay.h"
#include "usb_main.h"
#include "rfid_main.h"
#include "ble_main.h"
#include "syssleep.h"
#include "tag_emulation.h"
#include "hex_utils.h"
#include "data_cmd.h"
#include "app_cmd.h"


#define NRF_LOG_MODULE_NAME app_cmd
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();


data_frame_tx_t* cmd_processor_get_version(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint32_t version = 0xDEADBEEF;
    return data_frame_make(cmd, 0xDED1, 4, (uint8_t*)&version);
}

data_frame_tx_t* cmd_processor_change_device_mode(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length == 1) {
        if (data[0] == 1) {
            reader_mode_enter();
        } else {
            tag_mode_enter();
        }
    } else {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    return data_frame_make(cmd, 0x0000, 0, NULL);
}

data_frame_tx_t* cmd_processor_get_device_mode(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    device_mode_t mode = get_device_mode();
    if (mode == DEVICE_MODE_READER) {
        status = 1;
    } else {
        status = 0;
    }
    return data_frame_make(cmd, 0x0000, 1, (uint8_t*)&status);
}

data_frame_tx_t* cmd_processor_14a_scan(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    picc_14a_tag_t taginfo;
    status = pcd_14a_reader_scan_auto(&taginfo);
    if (status == HF_TAG_OK) {
        length = sizeof(picc_14a_tag_t);
        data = (uint8_t*)&taginfo;
    } else {
        length = 0;
        data = NULL;
    }
    return data_frame_make(cmd, status, length, data);
}

data_frame_tx_t* cmd_processor_detect_mf1_support(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    status = Check_STDMifareNT_Support();
    return data_frame_make(cmd, status, 0, NULL);
}

data_frame_tx_t* cmd_processor_detect_mf1_nt_level(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    status = Check_WeakNested_Support();
    return data_frame_make(cmd, status, 0, NULL);
}

data_frame_tx_t* cmd_processor_detect_mf1_darkside(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    status = Check_Darkside_Support();
    return data_frame_make(cmd, status, 0, NULL);
}

data_frame_tx_t* cmd_processor_mf1_darkside_acquire(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    DarksideCore dc;
	if (length == 4) {
		status = Darkside_Recover_Key(data[1], data[0], data[2], data[3], &dc);
		if (status == HF_TAG_OK) {
			length = sizeof(DarksideCore);
			data = (uint8_t *)(&dc);
		} else {
			length = 0;
		}
	} else {
		status = STATUS_PAR_ERR;
		length = 0;
	}
    return data_frame_make(cmd, status, length, data);
}

data_frame_tx_t* cmd_processor_detect_nested_dist(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    NestedDist nd;
	if (length == 8) {
		status = Nested_Distacne_Detect(data[1], data[0], &data[2], &nd);
		if (status == HF_TAG_OK) {
			// 探测完成
			length = sizeof(NestedDist);
			data = (uint8_t *)(&nd);
		} else {
			length = 0;
		}
	} else {
		status = STATUS_PAR_ERR;
		length = 0;
	}
    return data_frame_make(cmd, status, 0, NULL);
}

data_frame_tx_t* cmd_processor_mf1_nt_distance(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    NestedDist nd;
	if (length == 8) {
		status = Nested_Distacne_Detect(data[1], data[0], &data[2], &nd);
		if (status == HF_TAG_OK) {
			// 探测完成
			length = sizeof(NestedDist);
			data = (uint8_t *)(&nd);
		} else {
			length = 0;
		}
	} else {
		status = STATUS_PAR_ERR;
		length = 0;
	}
    return data_frame_make(cmd, status, length, data);
}

data_frame_tx_t* cmd_processor_mf1_nested_acquire(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    NestedCore ncs[SETS_NR];
	if (length == 10) {
		status = Nested_Recover_Key(bytes_to_num(&data[2], 6), data[1], data[0], data[9], data[8], ncs);
		if (status == HF_TAG_OK) {
			length = sizeof(ncs);
			data = (uint8_t *)(&ncs);
		} else {
			length = 0;
		}
	} else {
		status = STATUS_PAR_ERR;
		length = 0;
	}
    return data_frame_make(cmd, status, length, data);
}

data_frame_tx_t* cmd_processor_mf1_auth_one_key_block(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length == 8) {
		status = auth_key_use_522_hw(data[1], data[0], &data[2]);
        pcd_14a_reader_mf1_unauth();
	} else {
		status = STATUS_PAR_ERR;
	}
    return data_frame_make(cmd, status, 0, NULL);
}

data_frame_tx_t* cmd_processor_mf1_read_one_block(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t block[16] = { 0x00 };
	if (length == 8) {
		status = auth_key_use_522_hw(data[1], data[0], &data[2]);
		if (status == HF_TAG_OK) {
			// 直接调用标准读取API去读取卡片
			status = pcd_14a_reader_mf1_read(data[1], block);
			if (status == HF_TAG_OK) {
				length = 16;
			} else {
				length = 0;
			}
		} else {
			length = 0;
		}
	} else {
		length = 0;
		status = STATUS_PAR_ERR;
	}
    return data_frame_make(cmd, status, length, block);
}

data_frame_tx_t* cmd_processor_mf1_write_one_block(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length == 24) {
		status = auth_key_use_522_hw(data[1], data[0], &data[2]);
		if (status == HF_TAG_OK) {
			// 直接调用标准写入API去写入卡片
			status = pcd_14a_reader_mf1_write(data[1], &data[8]);
		} else {
			length = 0;
		}
	} else {
		status = STATUS_PAR_ERR;
	}
    return data_frame_make(cmd, status, 0, NULL);
}

data_frame_tx_t* cmd_processor_em410x_scan(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t id_buffer[5] = { 0x00 };
	status = PcdScanEM410X(id_buffer);
    return data_frame_make(cmd, status, sizeof(id_buffer), id_buffer);
}

data_frame_tx_t* cmd_processor_write_em410x_2_t57(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    // 写入T55XX的标签需要提供一个5个Byte长度的卡号
	// 并且还需要提供至少一个新的密钥，与一个旧的密钥
	if (length >= 13 && (length - 9) % 4 == 0) {
		status = PcdWriteT55XX(
			data, 				// 传入UID
			data + 5, 			// 传入newkey
			data + 9,			// 传入oldkey
			(length - 9) / 4 	// 传入减去newkey + uid后的剩余的oldkey的密钥组数
		);
	} else {
		status = STATUS_PAR_ERR;
	}
    return data_frame_make(cmd, status, 0, NULL);
}

// 封装一个自动切换卡槽的调用函数
static void change_slot_auto(uint8_t slot) {
    device_mode_t mode = get_device_mode();
    // 读卡器模式下不需要禁用模拟卡再进行切换
    tag_emulation_change_slot(slot, mode != DEVICE_MODE_READER);
    // 重新亮灯
    light_up_by_slot();
    // 默认亮起RGB
    set_slot_light_color(0);
}

data_frame_tx_t* cmd_processor_set_slot_activated(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    // 需要确保传过来的卡槽号码不要超过支持的上限
    if (length == 1 && data[0] < TAG_MAX_SLOT_NUM) {
        change_slot_auto(data[0]);
        status = STATUS_DEVICE_SUCCESS;
	} else {
        status = STATUS_PAR_ERR;
    }
    return data_frame_make(cmd, status, 0, NULL);
}

data_frame_tx_t* cmd_processor_set_slot_tag_type(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    // 需要确保传过来的标签类型是有效的
    if (length == 2 && data[0] < TAG_MAX_SLOT_NUM && data[1] != TAG_TYPE_UNKNOWN) {
        uint8_t num_slot = data[0];    // 获得要操作的卡槽
        uint8_t tag_type = data[1];    // 取出上位机传过来的标签类型
        // 将当前的卡槽切换到指定的模拟卡类型
        tag_emulation_change_type(num_slot, (tag_specific_type_t)tag_type);
		status = STATUS_DEVICE_SUCCESS;
	} else {
        status = STATUS_PAR_ERR;
    }
    return data_frame_make(cmd, status, 0, NULL);
}

data_frame_tx_t* cmd_processor_set_slot_data_default(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    // 需要确保传过来的标签类型是有效的
    if (length == 2 && data[0] < TAG_MAX_SLOT_NUM && data[1] != TAG_TYPE_UNKNOWN) {
        uint8_t num_slot = data[0];    // 获得要操作的卡槽
        uint8_t tag_type = data[1];    // 取出上位机传过来的标签类型
        // 重置当前的卡槽为缺省数据，如果失败，则可能是并未实现此API的缺省
        status = tag_emulation_factory_data(num_slot, (tag_specific_type_t)tag_type) ? STATUS_DEVICE_SUCCESS : STATUS_NOT_IMPLEMENTED;
	} else {
        status = STATUS_PAR_ERR;
    }
    return data_frame_make(cmd, status, 0, NULL);
}

data_frame_tx_t* cmd_processor_set_slot_enable(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    // 需要确保传过来的标签类型是有效的
    if (length == 2 && data[0] < TAG_MAX_SLOT_NUM && (data[1] == 0 || data[1] == 1)) {
        uint8_t slot_now = data[0];  // 获得要操作的卡槽
        bool enable = data[1];  // 获得要操作的卡槽的状态
        tag_emulation_slot_set_enable(slot_now, enable);
        if (!enable) {
            // 禁用了当前卡槽后，重新找一个能用的卡槽，切换到那个卡槽上
            uint8_t slot_prev = tag_emulation_slot_find_next(slot_now);
            NRF_LOG_INFO("slot_now = %d, slot_prev = %d", slot_now, slot_prev);
            if (slot_prev == slot_now) {
                // 找了一圈，发现并没有找到使能的卡槽，那么说明全部的卡槽都被禁用了
                // 此时我们应当灭掉卡槽灯
                set_slot_light_color(3);
            } else {
                change_slot_auto(slot_prev);
            }
        }
        status = STATUS_DEVICE_SUCCESS;
	} else {
        status = STATUS_PAR_ERR;
    }
    return data_frame_make(cmd, status, 0, NULL);
}

data_frame_tx_t* cmd_processor_set_em410x_emu_id(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    // 需要确保传过来的ID长度是对的
    if (length == LF_EM410X_TAG_ID_SIZE) {
        // 获取EM410x的缓冲区
        tag_data_buffer_t* buffer = get_buffer_by_tag_type(TAG_TYPE_EM410X);
        // 设置卡号进去
        memcpy(buffer->buffer, data, LF_EM410X_TAG_ID_SIZE);
        // 重新通知加载数据
        tag_emulation_load_by_buffer(TAG_TYPE_EM410X, false);
        status = STATUS_DEVICE_SUCCESS;
	} else {
        status = STATUS_PAR_ERR;
    }
    return data_frame_make(cmd, status, 0, NULL);
}

data_frame_tx_t* cmd_processor_set_mf1_detection_enable(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length == 1 && (data[0] == 0 || data[0] == 1)) {
        nfc_tag_mf1_detection_log_clear();  // 无论如何，操作日志的记录状态都要清除日志的历史记录
        nfc_tag_mf1_set_detection_enable(data[0]);
        status = STATUS_DEVICE_SUCCESS;
	} else {
        status = STATUS_PAR_ERR;
    }
    return data_frame_make(cmd, status, 0, NULL);
}

data_frame_tx_t* cmd_processor_get_mf1_detection_count(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint32_t count = nfc_tag_mf1_detection_log_count();
    if (count == 0xFFFFFFFF) {
        count = 0;
    }
    status = STATUS_DEVICE_SUCCESS;
    return data_frame_make(cmd, status, sizeof(uint32_t), (uint8_t *)&count);
}

data_frame_tx_t* cmd_processor_get_mf1_detection_log(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint32_t count;
    uint32_t index;
    uint8_t *resp = NULL;
    nfc_tag_mf1_auth_log_t* logs = get_mf1_auth_log(&count);
    if (length == 4) {
        if (count == 0xFFFFFFFF) {
            length = 0;
            status = STATUS_PAR_ERR;
        } else {
            index = bytes_to_num(data, 4);
            NRF_LOG_INFO("index = %d", index);
            if (index < count) {
                // 直接使用头部地址+index作为数据源
                resp = (uint8_t *)(logs + index);
                // 计算当前还能传输多少个日志
                length = MIN(count - index, DATA_PACK_MAX_DATA_LENGTH / sizeof(nfc_tag_mf1_auth_log_t)) * sizeof(nfc_tag_mf1_auth_log_t);
                status = STATUS_DEVICE_SUCCESS;
            } else {
                length = 0;
                status = STATUS_PAR_ERR;
            }
        }
    } else {
        length = 0;
        status = STATUS_PAR_ERR;
    }
    return data_frame_make(cmd, status, length, resp);
}

/**
 * before reader run, reset reader and on antenna,
 * we must to wait some time, to init picc(power).
 */
data_frame_tx_t* before_reader_run(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    device_mode_t mode = get_device_mode();
    if (mode == DEVICE_MODE_READER) {
        pcd_14a_reader_reset();
        pcd_14a_reader_antenna_on();
        bsp_delay_ms(8);
        return NULL;
    } else {
        return data_frame_make(cmd, STATUS_DEVIEC_MODE_ERROR, 0, NULL);
    }
}

/**
 * after reader run, off antenna, to keep battery.
 */
data_frame_tx_t* after_reader_run(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    pcd_14a_reader_antenna_off();
    return NULL;
}

/**
 * (cmd -> process) function map, the map struct is:
 *            cmd code                        before process               cmd processor                                after process
 */
static cmd_data_map_t m_data_cmd_map[] = {
    {    DATA_CMD_GET_APP_VERSION,            NULL,                        cmd_processor_get_version,                   NULL                },
    {    DATA_CMD_CHANGE_DEVICE_MODE,         NULL,                        cmd_processor_change_device_mode,            NULL                },
    {    DATA_CMD_GET_DEVICE_MODE,            NULL,                        cmd_processor_get_device_mode,               NULL                },

    {    DATA_CMD_SCAN_14A_TAG,               before_reader_run,           cmd_processor_14a_scan,                      after_reader_run    },
    {    DATA_CMD_MF1_SUPPORT_DETECT,         before_reader_run,           cmd_processor_detect_mf1_support,            after_reader_run    },
    {    DATA_CMD_MF1_NT_LEVEL_DETECT,        before_reader_run,           cmd_processor_detect_mf1_nt_level,           after_reader_run    },
    {    DATA_CMD_MF1_DARKSIDE_DETECT,        before_reader_run,           cmd_processor_detect_mf1_darkside,           after_reader_run    },

    {    DATA_CMD_MF1_DARKSIDE_ACQUIRE,       before_reader_run,           cmd_processor_mf1_darkside_acquire,          after_reader_run    },
    {    DATA_CMD_MF1_NT_DIST_DETECT,         before_reader_run,           cmd_processor_mf1_nt_distance,               after_reader_run    },
    {    DATA_CMD_MF1_NESTED_ACQUIRE,         before_reader_run,           cmd_processor_mf1_nested_acquire,            after_reader_run    },

    {    DATA_CMD_MF1_CHECK_ONE_KEY_BLOCK,    before_reader_run,           cmd_processor_mf1_auth_one_key_block,        after_reader_run    },
    {    DATA_CMD_MF1_READ_ONE_BLOCK,         before_reader_run,           cmd_processor_mf1_read_one_block,            after_reader_run    },
    {    DATA_CMD_MF1_WRITE_ONE_BLOCK,        before_reader_run,           cmd_processor_mf1_write_one_block,           after_reader_run    },

    {    DATA_CMD_SCAN_EM410X_TAG,            NULL,                        cmd_processor_em410x_scan,                   NULL                },
    {    DATA_CMD_WRITE_EM410X_TO_T5577,      NULL,                        cmd_processor_write_em410x_2_t57,            NULL                },
    
    {    DATA_CMD_SET_SLOT_ACTIVATED,         NULL,                        cmd_processor_set_slot_activated,            NULL                },
    {    DATA_CMD_SET_SLOT_TAG_TYPE,          NULL,                        cmd_processor_set_slot_tag_type,             NULL                },
    {    DATA_CMD_SET_SLOT_DATA_DEFAULT,      NULL,                        cmd_processor_set_slot_data_default,         NULL                },
    {    DATA_CMD_SET_SLOT_ENABLE,            NULL,                        cmd_processor_set_slot_enable,               NULL                },
    
    {    DATA_CMD_SET_EM410X_EMU_ID,          NULL,                        cmd_processor_set_em410x_emu_id,             NULL                },
    
    {    DATA_CMD_SET_MF1_DETECTION_ENABLE,   NULL,                        cmd_processor_set_mf1_detection_enable,      NULL                },
    {    DATA_CMD_GET_MF1_DETECTION_COUNT,    NULL,                        cmd_processor_get_mf1_detection_count,       NULL                },
    {    DATA_CMD_GET_MF1_DETECTION_RESULT,   NULL,                        cmd_processor_get_mf1_detection_log,         NULL                },
};


/**@brief Function for prcoess data frame(cmd)
 */
void on_data_frame_received(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    data_frame_tx_t* response = NULL;
    bool is_cmd_support = false;
    // print info
    NRF_LOG_INFO("Data frame: cmd = %02x, status = %02x, length = %d", cmd, status, length);
    NRF_LOG_HEXDUMP_INFO(data, length);
    for (int i = 0; i < ARRAY_SIZE(m_data_cmd_map); i++) {
        if (m_data_cmd_map[i].cmd == cmd) {
            is_cmd_support = true;
            if (m_data_cmd_map[i].cmd_before != NULL) {
                data_frame_tx_t* before_resp = m_data_cmd_map[i].cmd_before(cmd, status, length, data);
                if (before_resp != NULL) {
                    // some problem found before run cmd.
                    response = before_resp;
                    break;
                }
            }
            if (m_data_cmd_map[i].cmd_processor != NULL) response = m_data_cmd_map[i].cmd_processor(cmd, status, length, data);
            if (m_data_cmd_map[i].cmd_after != NULL) {
                data_frame_tx_t* after_resp = m_data_cmd_map[i].cmd_after(cmd, status, length, data);
                if (after_resp != NULL) {
                    // some problem found after run cmd.
                    response = after_resp;
                    break;
                }
            }
            break;
        }
    }
    if (is_cmd_support) {
        // check and response
        if (response != NULL) {
            usb_cdc_write(response->buffer, response->length);
        }
    } else {
        // response cmd unsupport.
        response = data_frame_make(cmd, STATUS_INVALID_CMD, 0, NULL);
        usb_cdc_write(response->buffer, response->length);
        NRF_LOG_INFO("Data frame cmd invalid: %d,", cmd);
    }
}
