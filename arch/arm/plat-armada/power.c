/*
 * arch/arm/plat-armada/power.c
 *
 * CPU power management functions
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/cpuidle.h>
#include <asm/io.h>
#include <asm/proc-fns.h>
#include <plat/cache-aurora-l2.h>
#include <mach/smp.h>
#include <asm/vfp.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/pgalloc.h>
#include <asm/sections.h>

#include "ctrlEnv/sys/mvCpuIfRegs.h"
#include "mvOs.h"

static void hw_sem_lock(void)
{
	unsigned int cpu = hard_smp_processor_id();

	while(cpu != (readb(INTER_REGS_BASE + MV_CPU_HW_SEM_OFFSET) & 0xf));
}

static void hw_sem_unlock(void)
{
	writeb(0xff, INTER_REGS_BASE + MV_CPU_HW_SEM_OFFSET);
}

extern int armadaxp_cpu_resume(void);
u32 cib_ctrl_cfg_reg;

void armadaxp_fabric_setup_deepIdle(void)
{
	MV_U32  reg;
	MV_U32	i;

	reg = MV_REG_READ(MV_L2C_NFABRIC_PM_CTRL_CFG_REG);
	reg |= MV_L2C_NFABRIC_PM_CTRL_CFG_PWR_DOWN;
	MV_REG_WRITE(MV_L2C_NFABRIC_PM_CTRL_CFG_REG, reg);

	for (i=0; i<4; i++) {
		/* Enable L2 & Fabric powerdown in Deep-Idle mode */
		reg = MV_REG_READ(PM_CONTROL_AND_CONFIG_REG(i));
		reg |= PM_CONTROL_AND_CONFIG_L2_PWDDN;
		MV_REG_WRITE(PM_CONTROL_AND_CONFIG_REG(i), reg);
	}

	/* Configure CPU_DivClk_Control0 */
	reg = MV_REG_READ(0x18700);
	reg &= ~0xFFFF00;
	reg |= 0x10EF00;
	MV_REG_WRITE(0x18700, reg); 
	
	/* Configure  PMU_DFS_Control_1 */
	reg = MV_REG_READ(0x1C054);
	reg &= 0xFF000000;
	reg >>= 24;
	reg = (reg << 24) | ((reg + 1) << 16) | 0x10404;
	MV_REG_WRITE(0x1C054, reg);

	/* Configure  PMU Program registers */
	MV_REG_WRITE(0x1C270, 0x00c108a8);
	MV_REG_WRITE(0x1C274, 0x0000005a);
	MV_REG_WRITE(0x1C278, 0x00000000);
	MV_REG_WRITE(0x1C27c, 0x195b0000);
	MV_REG_WRITE(0x1C280, 0x00ff0014);

#ifdef CONFIG_ARMADA_XP_DEEP_IDLE_L2_WA
	/* disable HW L2C Flush configure 0x22008:
	 *  Set bit 4 -> Will skip HW L2C flush triggering.
	 *  Set bit 5 -> Will skip waiting for HW L2C flush done indication.
	 */
	reg = MV_REG_READ(MV_L2C_NFABRIC_PWR_DOWN_FLOW_CTRL_REG);
	reg |= 3 << 4;

	/* Configure skiping the RA & WA Disable */
	reg |= 1;
	/* Configure skiping the Sharing Disable */
	reg |= 1 << 8;
	/* Configure skiping the CIB Ack Disable */
	reg |= 1 << 6;
	/* Configure skiping the RA & WA Resume */
	reg |= 1 << 12;
	/* Configure skiping the CIB Ack Resume */
	reg |= 1 << 15;
	/* Configure skiping the Sharing Resume */
	reg |= 1 << 14;

	MV_REG_WRITE(MV_L2C_NFABRIC_PWR_DOWN_FLOW_CTRL_REG, reg);
#endif

#ifdef CONFIG_ARMADA_XP_DEEP_IDLE_L2_WA
	/* neet to restore this register on resume */
	cib_ctrl_cfg_reg = MV_REG_READ(MV_CIB_CTRL_CFG_REG);
#endif
	
	/* Set the resume control registers to do nothing */
	MV_REG_WRITE(0x20980, 0);
	MV_REG_WRITE(0x20988, 0);

	/* configure the MPP29 used for CPU0+L2+Fabric power control*/
	reg = MV_REG_READ(MPP_CONTROL_REG(3));
	reg &= ~0x00F00000;
	reg |= 0x00500000;
	MV_REG_WRITE(MPP_CONTROL_REG(3), reg);

	/* configure the MPP40 used for CPU1 power control*/
	reg = MV_REG_READ(MPP_CONTROL_REG(5));
	reg &= ~0x0000000F;
	reg |= 0x00000003;
	MV_REG_WRITE(MPP_CONTROL_REG(5), reg);

	/* configure the MPP57 used for CPU2+3 power control*/
	reg = MV_REG_READ(MPP_CONTROL_REG(7));
	reg &= ~0x000000F0;
	reg |= 0x00000020;
	MV_REG_WRITE(MPP_CONTROL_REG(7), reg);
}
EXPORT_SYMBOL(armadaxp_fabric_setup_deepIdle);

void armadaxp_fabric_prepare_deepIdle(void)
{
	unsigned int processor_id = hard_smp_processor_id();
	MV_U32  reg;

	MV_REG_WRITE(PM_CPU_BOOT_ADDR_REDIRECT(processor_id), virt_to_phys(armadaxp_cpu_resume));
#if defined CONFIG_AURORA_IO_CACHE_COHERENCY || CONFIG_SMP
	hw_sem_lock();
	/* Disable delivery of snoop requests to the CPU core by setting */
	reg = MV_REG_READ(MV_COHERENCY_FABRIC_CTRL_REG);
	reg &= ~(1 << (24 + processor_id));
	MV_REG_WRITE(MV_COHERENCY_FABRIC_CTRL_REG, reg);
	hw_sem_unlock();
#endif

	reg = MV_REG_READ(PM_STATUS_AND_MASK_REG(processor_id));
	/* set WaitMask fields */
	reg |= PM_STATUS_AND_MASK_CPU_IDLE_WAIT;
	reg |= PM_STATUS_AND_MASK_SNP_Q_EMPTY_WAIT;
	/* Enable wakeup events */
	reg |= PM_STATUS_AND_MASK_IRQ_WAKEUP | PM_STATUS_AND_MASK_FIQ_WAKEUP;
//	reg |= PM_STATUS_AND_MASK_DBG_WAKEUP;

#ifdef CONFIG_ARMADA_XP_DEEP_IDLE_UNMASK_INTS_WA
	/* don't mask interrupts due to known issue */
#else
	/* Mask interrupts */
	reg |= PM_STATUS_AND_MASK_IRQ_MASK | PM_STATUS_AND_MASK_FIQ_MASK;
#endif
	MV_REG_WRITE(PM_STATUS_AND_MASK_REG(processor_id), reg);

	/* Disable delivering of other CPU core cache maintenance instruction,
	 * TLB, and Instruction synchronization to the CPU core 
	 */
	/* TODO */
#ifdef CONFIG_CACHE_AURORA_L2
	/* ask HW to power down the L2 Cache if possible */
	reg = MV_REG_READ(PM_CONTROL_AND_CONFIG_REG(processor_id));
	reg |= PM_CONTROL_AND_CONFIG_L2_PWDDN;
	MV_REG_WRITE(PM_CONTROL_AND_CONFIG_REG(processor_id), reg);
#endif

	/* request power down */
	reg = MV_REG_READ(PM_CONTROL_AND_CONFIG_REG(processor_id));
	reg |= PM_CONTROL_AND_CONFIG_PWDDN_REQ;
	MV_REG_WRITE(PM_CONTROL_AND_CONFIG_REG(processor_id), reg);

#ifdef CONFIG_ARMADA_XP_DEEP_IDLE_L2_WA
	/* Disable RA & WA allocate */
	reg = MV_REG_READ(MV_CIB_CTRL_CFG_REG);
	reg &= ~0x1E;
	reg |= 0x12;
	MV_REG_WRITE(MV_CIB_CTRL_CFG_REG, reg);

	auroraL2_flush_all();

	/* Disable CIB Ack */
	reg = MV_REG_READ(MV_CIB_CTRL_CFG_REG);
	reg |= 1 << 9;
	MV_REG_WRITE(MV_CIB_CTRL_CFG_REG, reg);
	
	/* wait for CIB empty */
	udelay(1);

	/* Disable CIB Sharing */
	reg = MV_REG_READ(MV_CIB_CTRL_CFG_REG);
	reg &=  ~(3 << 10);
	reg |= 0x2 << 10;
	MV_REG_WRITE(MV_CIB_CTRL_CFG_REG, reg);
#endif
}
EXPORT_SYMBOL(armadaxp_fabric_prepare_deepIdle);

void armadaxp_fabric_restore_deepIdle(void)
{
	unsigned int processor_id = hard_smp_processor_id();
	MV_U32  reg;

	/* cancel request power down */
	reg = MV_REG_READ(PM_CONTROL_AND_CONFIG_REG(processor_id));
	reg &= ~PM_CONTROL_AND_CONFIG_PWDDN_REQ;
	MV_REG_WRITE(PM_CONTROL_AND_CONFIG_REG(processor_id), reg);

#ifdef CONFIG_CACHE_AURORA_L2
	/* cancel ask HW to power down the L2 Cache if possible */
	reg = MV_REG_READ(PM_CONTROL_AND_CONFIG_REG(processor_id));
	reg &= ~PM_CONTROL_AND_CONFIG_L2_PWDDN;
	MV_REG_WRITE(PM_CONTROL_AND_CONFIG_REG(processor_id), reg);
#endif
	/* cancel Disable delivering of other CPU core cache maintenance instruction,
	 * TLB, and Instruction synchronization to the CPU core 
	 */
	/* TODO */

	/* cancel Enable wakeup events */
	reg = MV_REG_READ(PM_STATUS_AND_MASK_REG(processor_id));
	reg &= ~(PM_STATUS_AND_MASK_IRQ_WAKEUP | PM_STATUS_AND_MASK_FIQ_WAKEUP);
	reg &= ~PM_STATUS_AND_MASK_CPU_IDLE_WAIT;
	reg &= ~PM_STATUS_AND_MASK_SNP_Q_EMPTY_WAIT;
//	reg &= ~PM_STATUS_AND_MASK_DBG_WAKEUP;

	/* Mask interrupts */
	reg &= ~(PM_STATUS_AND_MASK_IRQ_MASK | PM_STATUS_AND_MASK_FIQ_MASK);
	MV_REG_WRITE(PM_STATUS_AND_MASK_REG(processor_id), reg);
#if defined CONFIG_AURORA_IO_CACHE_COHERENCY || CONFIG_SMP
	/* cancel Disable delivery of snoop requests to the CPU core by setting */
	hw_sem_lock();
	reg = MV_REG_READ(MV_COHERENCY_FABRIC_CTRL_REG);
	reg |= 1 << (24 + processor_id);
	MV_REG_WRITE(MV_COHERENCY_FABRIC_CTRL_REG, reg);
	hw_sem_unlock();
#endif
#ifdef CONFIG_ARMADA_XP_DEEP_IDLE_L2_WA
	/* restore CIB  Control and Configuration register except CIB Ack */
	MV_REG_WRITE(MV_CIB_CTRL_CFG_REG, cib_ctrl_cfg_reg & ~(1 << 9));
	/* add delay needed for erratum "Dunit MBus starvation causes poor performance during deep-idle " */
	udelay(10);
	/* restore CIB Control including CIB Ack */
	MV_REG_WRITE(MV_CIB_CTRL_CFG_REG, cib_ctrl_cfg_reg);
#endif
}
EXPORT_SYMBOL(armadaxp_fabric_restore_deepIdle);

void armadaxp_smp_prepare_idle(unsigned int processor_id)
{
	u32 reg;

	flush_cache_all();

	/* Disable delivery of snoop requests to the CPU core */
	hw_sem_lock();
	reg = MV_REG_READ(MV_COHERENCY_FABRIC_CTRL_REG);
	reg &= ~(1 << (24 + processor_id));
	MV_REG_WRITE(MV_COHERENCY_FABRIC_CTRL_REG, reg);
	MV_REG_READ(MV_COHERENCY_FABRIC_CTRL_REG);
	hw_sem_unlock();
	udelay(1);
}
EXPORT_SYMBOL(armadaxp_smp_prepare_idle);

void armadaxp_smp_restore_idle(unsigned int processor_id)
{
	u32 reg;

	/* Enable delivery of snoop requests to the CPU core */
	hw_sem_lock();
	reg = MV_REG_READ(MV_COHERENCY_FABRIC_CTRL_REG);
	reg |= 1 << (24 + processor_id);
	MV_REG_WRITE(MV_COHERENCY_FABRIC_CTRL_REG, reg);
	MV_REG_READ(MV_COHERENCY_FABRIC_CTRL_REG);
	hw_sem_unlock();
}
EXPORT_SYMBOL(armadaxp_smp_restore_idle);
