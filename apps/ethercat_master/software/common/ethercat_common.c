#include "ethercat_common.h"

#define ESC_TIMER          (HPM_GPTMR3)
#define ESC_TIMER_CH       1
#define ESC_TIMER_IRQ      IRQn_GPTMR3
#define ESC_TIMER_CLK_NAME (clock_gptmr3)

volatile bool esc_pdo_error = false;
volatile bool esc_enter_op = false;

char IOmap[512];

void esc_timer_stop();

void esc_timer_isr(void)
{
    uint16 wkc;

    if (gptmr_check_status(ESC_TIMER, GPTMR_CH_RLD_STAT_MASK(ESC_TIMER_CH))) {
        gptmr_clear_status(ESC_TIMER, GPTMR_CH_RLD_STAT_MASK(ESC_TIMER_CH));
        ec_send_processdata();
        wkc = ec_receive_processdata(200);
        esc_pdo_error = false;
        if ((wkc / 3) != ec_slavecount) {
            esc_pdo_error = true;
            printf("wkc error, expect %d but %d!\n", ec_slavecount, wkc);
            esc_timer_stop();
        }
    }
}
SDK_DECLARE_EXT_ISR_M(ESC_TIMER_IRQ, esc_timer_isr);

void esc_timer_create(uint32_t us)
{
    uint32_t gptmr_freq;
    gptmr_channel_config_t config;

    gptmr_channel_get_default_config(ESC_TIMER, &config);

    clock_add_to_group(ESC_TIMER_CLK_NAME, 0);
    gptmr_freq = clock_get_frequency(ESC_TIMER_CLK_NAME);

    config.reload = gptmr_freq / 1000000 * us;
    gptmr_channel_config(ESC_TIMER, ESC_TIMER_CH, &config, false);
    gptmr_enable_irq(ESC_TIMER, GPTMR_CH_RLD_IRQ_MASK(ESC_TIMER_CH));
    intc_m_enable_irq_with_priority(ESC_TIMER_IRQ, 1);

    gptmr_start_counter(ESC_TIMER, ESC_TIMER_CH);
}

void esc_timer_stop(void)
{
    gptmr_stop_counter(ESC_TIMER, ESC_TIMER_CH);
    gptmr_disable_irq(ESC_TIMER, GPTMR_CH_RLD_IRQ_MASK(ESC_TIMER_CH));
    intc_m_disable_irq(ESC_TIMER_IRQ);
}

int ethercat_common_start(esc_sdo_config_t sdo_config)
{
    int cnt, i, j, slc, try_count;

    /* initialise SOEM */
    if (ec_init(NULL) > 0) {
        printf("ec_init succeeded.\n");

        /* find and auto-config slaves */
        if (ec_config_init(FALSE) > 0) {
            printf("%d slaves found and configured.\n", ec_slavecount);

            for (slc = 1; slc <= ec_slavecount; slc++) {
                printf("Found %s at position %d\n", ec_slave[slc].name, slc);
            }

            if (sdo_config) {
                sdo_config();
            }

            ec_configdc();
            ec_dcsync0(1, TRUE, 1 * 1000000, 20000); // SYNC0 on slave 1

            uint32 dc_time;
            for (uint16_t i = 0; i < 16000; i++) {
                dc_time = 0;
                ec_FRMW(ec_slave[1].configadr, ECT_REG_DCSYSTIME, 4, &dc_time, EC_TIMEOUTRET);
            }

            slc = 0;

            /* Run IO mapping */
            ec_config_map(&IOmap);

            printf("Slaves mapped, state to SAFE_OP.\n");
            /* wait for all slaves to reach SAFE_OP state */
            ec_statecheck(slc, EC_STATE_SAFE_OP, EC_TIMEOUTSTATE);
            ec_readstate();
            printf("Slave 0 State=0x%04x\r\n", ec_slave[0].state);
            printf("Slave 1 State=0x%04x\r\n", ec_slave[1].state);

            /* Print som information on the mapped network */
            for (cnt = 1; cnt <= ec_slavecount; cnt++) {
                printf("\nSlave:%d\n Name:%s\n Output size: %dbits\n Input size: %dbits\n State: %d\n Delay: %d[ns]\n Has DC: %d\n",
                       cnt, ec_slave[cnt].name, ec_slave[cnt].Obits, ec_slave[cnt].Ibits,
                       ec_slave[cnt].state, ec_slave[cnt].pdelay, ec_slave[cnt].hasdc);
                printf(" Configured address: %x\n", ec_slave[cnt].configadr);
                printf(" Outputs address: %x\n", ec_slave[cnt].outputs);
                printf(" Inputs address: %x\n", ec_slave[cnt].inputs);

                for (j = 0; j < ec_slave[cnt].FMMUunused; j++) {
                    printf(" FMMU%1d Ls:%x Ll:%4d Lsb:%d Leb:%d Ps:%x Psb:%d Ty:%x Act:%x\n", j,
                           (int)ec_slave[cnt].FMMU[j].LogStart, ec_slave[cnt].FMMU[j].LogLength, ec_slave[cnt].FMMU[j].LogStartbit,
                           ec_slave[cnt].FMMU[j].LogEndbit, ec_slave[cnt].FMMU[j].PhysStart, ec_slave[cnt].FMMU[j].PhysStartBit,
                           ec_slave[cnt].FMMU[j].FMMUtype, ec_slave[cnt].FMMU[j].FMMUactive);
                }
                printf(" FMMUfunc 0:%d 1:%d 2:%d 3:%d\n",
                       ec_slave[cnt].FMMU0func, ec_slave[cnt].FMMU1func, ec_slave[cnt].FMMU2func, ec_slave[cnt].FMMU3func);
            }

            printf("Request operational state for all slaves\n");
            esc_timer_create(1000);

            try_count = 0;
            ec_slave[slc].state = EC_STATE_OPERATIONAL;
            ec_writestate(slc);
            do {
                disable_global_irq(CSR_MSTATUS_MIE_MASK);
                ec_statecheck(slc, EC_STATE_OPERATIONAL, 0);
                enable_global_irq(CSR_MSTATUS_MIE_MASK);
                if (try_count++ >= 5000) {
                    printf("timeout!\r\n");
                    esc_timer_stop();
                    return -1;
                }
                board_delay_us(1000);
            } while (ec_slave[slc].state != EC_STATE_OPERATIONAL);

            printf("Slave 0 State=0x%04x\r\n", ec_slave[slc].state);

            if (ec_slave[slc].state == EC_STATE_OPERATIONAL) {
                esc_enter_op = true;
                printf("Operational state reached for all slaves.\n");
                return 0;
            } else {
                printf("Not all slaves reached operational state.\n");
                ec_readstate();
                for (i = 1; i <= ec_slavecount; i++) {
                    if (ec_slave[i].state != EC_STATE_OPERATIONAL) {
                        printf("Slave %d State=0x%04x StatusCode=0x%04x\n",
                               i, ec_slave[i].state, ec_slave[i].ALstatuscode);
                    }
                }
                return -1;
            }
        } else {
            printf("ec_config_init failed\n");
            return -1;
        }
    }
    return -1;
}

void ethercat_common_stop(void)
{
    esc_enter_op = false;
    esc_timer_stop();
}