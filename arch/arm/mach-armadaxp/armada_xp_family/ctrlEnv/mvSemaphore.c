/*******************************************************************************
Copyright (C) Marvell International Ltd. and its affiliates

This software file (the "File") is owned and distributed by Marvell 
International Ltd. and/or its affiliates ("Marvell") under the following
alternative licensing terms.  Once you have made an election to distribute the
File under one of the following license alternatives, please (i) delete this
introductory statement regarding license alternatives, (ii) delete the two
license alternatives that you have not elected to use and (iii) preserve the
Marvell copyright notice above.

********************************************************************************
Marvell Commercial License Option

If you received this File from Marvell and you have entered into a commercial
license agreement (a "Commercial License") with Marvell, the File is licensed
to you under the terms of the applicable Commercial License.

********************************************************************************
Marvell GPL License Option

If you received this File from Marvell, you may opt to use, redistribute and/or 
modify this File in accordance with the terms and conditions of the General 
Public License Version 2, June 1991 (the "GPL License"), a copy of which is 
available along with the File in the license.txt file or by writing to the Free 
Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 or 
on the worldwide web at http://www.gnu.org/licenses/gpl.txt. 

THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE IMPLIED 
WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE ARE EXPRESSLY 
DISCLAIMED.  The GPL License provides additional details about this warranty 
disclaimer.
********************************************************************************
Marvell BSD License Option

If you received this File from Marvell, you may opt to use, redistribute and/or 
modify this File under the following licensing terms. 
Redistribution and use in source and binary forms, with or without modification, 
are permitted provided that the following conditions are met:

    *   Redistributions of source code must retain the above copyright notice,
	    this list of conditions and the following disclaimer. 

    *   Redistributions in binary form must reproduce the above copyright
        notice, this list of conditions and the following disclaimer in the
        documentation and/or other materials provided with the distribution. 

    *   Neither the name of Marvell nor the names of its contributors may be 
        used to endorse or promote products derived from this software without 
        specific prior written permission. 
    
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR 
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES 
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON 
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*******************************************************************************/

#include "ctrlEnv/mvCtrlEnvLib.h"
#include "ctrlEnv/sys/mvCpuIf.h"
#include "cpu/mvCpu.h"
#include "ctrlEnv/mvSemaphore.h"


MV_BOOL mvSemaLock(MV_32 num)
{
	MV_U32 tmp;
	MV_U32 cpuId;
	if (num > MV_MAX_SEMA)
	{
		mvOsPrintf("Invalid semaphore number\n");
		return MV_FALSE;
	}
	cpuId = whoAmI();
	do
	{
		tmp = MV_REG_BYTE_READ(MV_SEMA_REG_BASE+num);
	} while ((tmp & 0xFF) != cpuId);
	return MV_TRUE;
}

MV_BOOL mvSemaTryLock(MV_32 num)
{
	MV_U32 tmp;
	if (num > MV_MAX_SEMA)
	{
		mvOsPrintf("Invalid semaphore number\n");
		return MV_FALSE;
	}
	tmp = MV_REG_BYTE_READ(MV_SEMA_REG_BASE+num);
	if ((tmp & 0xFF) != whoAmI())
	{
		return MV_FALSE;
	}
	else
		return MV_TRUE;
}

MV_BOOL mvSemaUnlock(MV_32 num)
{
	if (num > MV_MAX_SEMA)
	{
		mvOsPrintf("Invalid semaphore number\n");
		return MV_FALSE;
	}
	MV_REG_BYTE_WRITE(MV_SEMA_REG_BASE+(num), 0xFF);
	return MV_TRUE;
}


MV_VOID mvHwBarrier(MV_32 cpuCount)
{
	MV_U32 cpuId;
	MV_U32 myCpuId = whoAmI();

	/* Let all the rest know i arrived */
	mvSemaLock(MV_SEMA_BARRIER(myCpuId));

	cpuCount--;
	
	/* Now try to find all the rest */
	while(cpuCount > 0){
		/* Scan all CPUs to see who arrived */
		for(cpuId = 0; cpuId < NR_CPUS; cpuId++){
			if(cpuId == myCpuId)
				continue;

			if(mvSemaTryLock(MV_SEMA_BARRIER(cpuId)) == MV_TRUE)
				mvSemaUnlock(MV_SEMA_BARRIER(cpuId));
			else
				cpuCount--;
		}

		/* Wait some cycles before retry to avoid overloading bus */
		if(cpuCount > 0)
			udelay(1);
	}

	/* Release my semaphore so we can use it again */
	/* wait a little before leaving to allow other to see you */
	udelay(1);
	mvSemaUnlock(MV_SEMA_BARRIER(myCpuId));
}