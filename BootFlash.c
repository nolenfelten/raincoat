/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

 /* 
  2003-01-27  andy@warmcat.com  Fixed and verified ST29F080A programming
                                Different block erase command from SST part
                                Also seemed to require explicit chip reset action
  2003-01-06  andy@warmcat.com  Created
 */

#include "boot.h"
#include "BootFlash.h"
#include <stdio.h>

	// gets device ID, sets pof up accordingly
	// returns true if device okay or false for unrecognized device

bool BootFlashGetDescriptor( OBJECT_FLASH *pof, KNOWN_FLASH_TYPE * pkft )
{
	bool fSeen=false;
	BYTE baNormalModeFirstTwoBytes[2];
	int nTries=0;

	baNormalModeFirstTwoBytes[0]=pof->m_pbMemoryMappedStartAddress[0];
	baNormalModeFirstTwoBytes[1]=pof->m_pbMemoryMappedStartAddress[1];

	while(nTries++ <2) { // first we try 29xxx method, then 28xxx if that failed

			// no ISRs should touch flash while we do the stuff
		__asm__ __volatile__ ( "pushf ; cli ");

		
		if(nTries==1) { // 29xxx protocol
		
			// make sure the flash state machine is reset

			pof->m_pbMemoryMappedStartAddress[0x5555]=0xf0;
			pof->m_pbMemoryMappedStartAddress[0x5555]=0xaa;
			pof->m_pbMemoryMappedStartAddress[0x2aaa]=0x55;
			pof->m_pbMemoryMappedStartAddress[0x5555]=0xf0;

				// read flash ID

			pof->m_pbMemoryMappedStartAddress[0x5555]=0xaa;
			pof->m_pbMemoryMappedStartAddress[0x2aaa]=0x55;
			pof->m_pbMemoryMappedStartAddress[0x5555]=0x90;

			pof->m_bManufacturerId=pof->m_pbMemoryMappedStartAddress[0];
			pof->m_bDeviceId=pof->m_pbMemoryMappedStartAddress[1];

			pof->m_pbMemoryMappedStartAddress[0x5555]=0xf0;

		} else { // 28xxx protocol, seen on Sharp

				// make sure the flash state machine is reset

			pof->m_pbMemoryMappedStartAddress[0x5555]=0xff;
				
				// read flash ID

			pof->m_pbMemoryMappedStartAddress[0x5555]=0x90;
			pof->m_bManufacturerId=pof->m_pbMemoryMappedStartAddress[0];

			pof->m_pbMemoryMappedStartAddress[0x5555]=0x90;
			pof->m_bDeviceId=pof->m_pbMemoryMappedStartAddress[1];

			pof->m_pbMemoryMappedStartAddress[0x5555]=0xff;
			
			pof->m_fDetectedUsing28xxxConventions=true; // mark the flash object as representing a 29xxx job

		}

		__asm__ __volatile__ ( "popf ");


		if(
			(baNormalModeFirstTwoBytes[0]!=pof->m_bManufacturerId) ||
			(baNormalModeFirstTwoBytes[1]!=pof->m_pbMemoryMappedStartAddress[1])
		) nTries=2;  // don't try any more if we got some result the first time

	} // while


		// interpret device ID info

	{
		bool fMore=true;
		while(fMore) {
			if(!pkft->m_bManufacturerId) {
				fMore=false; continue;
			}
			if((pkft->m_bManufacturerId == pof->m_bManufacturerId) &&
				(pkft->m_bDeviceId == pof->m_bDeviceId)
			) {
				fSeen=true;
				fMore=false;
				sprintf(pof->m_szFlashDescription, "%s (%ldK)", pkft->m_szFlashDescription, pkft->m_dwLengthInBytes/1024);
				pof->m_dwLengthInBytes = pkft->m_dwLengthInBytes;
			}
			pkft++;
		}
	}

	if(!fSeen) {
		if(
			(baNormalModeFirstTwoBytes[0]==pof->m_bManufacturerId) &&
			(baNormalModeFirstTwoBytes[1]==pof->m_pbMemoryMappedStartAddress[1])
		) { // we didn't get anything worth reporting
			sprintf(pof->m_szFlashDescription, "Read Only??? manf=0x%02X, dev=0x%02X", pof->m_bManufacturerId, pof->m_bDeviceId);
		} else { // we got what is probably an unknown flash type
			sprintf(pof->m_szFlashDescription, "manf=0x%02X, dev=0x%02X", pof->m_bManufacturerId, pof->m_bDeviceId);
		}
	}

	return fSeen;
}

 // uses the block erase function on the flash to erase the minimal footprint
 // needed to cover pof->m_dwStartOffset .. (pof->m_dwStartOffset+pof->m_dwLengthUsedArea)

bool BootFlashEraseMinimalRegion( OBJECT_FLASH *pof )
{
	DWORD dw=pof->m_dwStartOffset;
	DWORD dwLen=pof->m_dwLengthUsedArea;
	DWORD dwLastEraseAddress=0xffffffff;
	int nCountEraseRetryIn4KBlock=4;

	if(pof->m_pcallbackFlash!=NULL) if(!(pof->m_pcallbackFlash)(pof, EE_ERASE_START, 0, 0)) return false;

	while(dwLen) {

		if(pof->m_pbMemoryMappedStartAddress[dw]!=0xff) { // something needs erasing

			if((dwLastEraseAddress & 0xfffff000)==(dw & 0xfffff000)) { // same 4K block?
				nCountEraseRetryIn4KBlock--;
				if(nCountEraseRetryIn4KBlock==0) { // run out of tries in this 4K block
					if(pof->m_pcallbackFlash!=NULL) if(!(pof->m_pcallbackFlash)(pof, EE_ERASE_ERROR, dw-pof->m_dwStartOffset, pof->m_pbMemoryMappedStartAddress[dw])) return false;
					if(pof->m_pcallbackFlash!=NULL) if(!(pof->m_pcallbackFlash)(pof, EE_ERASE_END, 0, 0)) return false;
					return false; // failure
				}
			} else {
				nCountEraseRetryIn4KBlock=4;  // different block, reset retry counter
				dwLastEraseAddress=dw;
			}


				// no ISRs should touch flash while we do the stuff
			
			__asm__ __volatile__ ( "pushf ; cli ");


			if(pof->m_fDetectedUsing28xxxConventions) {
				pof->m_pbMemoryMappedStartAddress[0x5555]=0x20;
				pof->m_pbMemoryMappedStartAddress[dw]=0xd0; // erase the block containing the non 0xff guy
			} else {
				pof->m_pbMemoryMappedStartAddress[0x5555]=0xaa;
				pof->m_pbMemoryMappedStartAddress[0x2aaa]=0x55;
				pof->m_pbMemoryMappedStartAddress[0x5555]=0x80;

				pof->m_pbMemoryMappedStartAddress[0x5555]=0xaa;
				pof->m_pbMemoryMappedStartAddress[0x2aaa]=0x55;
				pof->m_pbMemoryMappedStartAddress[dw]=0x50; // erase the block containing the non 0xff guy
			}

				// wait out erase period
			{
				BYTE b=pof->m_pbMemoryMappedStartAddress[dw];  // waits until b6 is no longer toggling on each read
				DWORD dwCountTries=0;
				while((pof->m_pbMemoryMappedStartAddress[dw]&0x40)!=(b&0x40)) {
					dwCountTries++; b^=0x40;
				}
				if((dwCountTries<3) && (!pof->m_fDetectedUsing28xxxConventions)) { // this can't be right - maybe this flash uses different block erase code
					pof->m_pbMemoryMappedStartAddress[0x5555]=0xaa;
					pof->m_pbMemoryMappedStartAddress[0x2aaa]=0x55;
					pof->m_pbMemoryMappedStartAddress[0x5555]=0xf0;

					pof->m_pbMemoryMappedStartAddress[0x5555]=0xaa;
					pof->m_pbMemoryMappedStartAddress[0x2aaa]=0x55;
					pof->m_pbMemoryMappedStartAddress[0x5555]=0x80;

					pof->m_pbMemoryMappedStartAddress[0x5555]=0xaa;
					pof->m_pbMemoryMappedStartAddress[0x2aaa]=0x55;
					pof->m_pbMemoryMappedStartAddress[dw]=0x30; // erase the block containing the non 0xff guy

					b=pof->m_pbMemoryMappedStartAddress[dw];
					while((pof->m_pbMemoryMappedStartAddress[dw]&0x40)!=(b&0x40)) {
						dwCountTries++; b^=0x40;
					}
				}
			}

			__asm__ __volatile__ ( "popf ");

			continue; // retry reading this address without moving on
		}

			// update callback every 1K addresses
		if((dw&0x3ff)==0) if(pof->m_pcallbackFlash!=NULL) if(!(pof->m_pcallbackFlash)(pof, EE_ERASE_UPDATE, dw-pof->m_dwStartOffset, pof->m_dwLengthUsedArea)) return false;

		dwLen--; dw++;
	}

	if(pof->m_pcallbackFlash!=NULL) if(!(pof->m_pcallbackFlash)(pof, EE_ERASE_END, 0, 0)) return false;

	return true;
}

	// program the flash from the data in pba
	// length of valid data in pba held in pof->m_dwLengthUsedArea

bool BootFlashProgram( OBJECT_FLASH *pof, BYTE *pba )
{
	DWORD dw=pof->m_dwStartOffset;
	DWORD dwLen=pof->m_dwLengthUsedArea;
	DWORD dwSrc=0;
	DWORD dwLastProgramAddress=0xffffffff;
	int nCountProgramRetries=4;

	if(pof->m_pcallbackFlash!=NULL) if(!(pof->m_pcallbackFlash)(pof, EE_PROGRAM_START, 0, 0)) return false;

		// program

	while(dwLen) {

		if(pof->m_pbMemoryMappedStartAddress[dw]!=pba[dwSrc]) { // needs programming

			if(dwLastProgramAddress==dw) {
				nCountProgramRetries--;
				if(nCountProgramRetries==0) {
					if(pof->m_pcallbackFlash!=NULL) if(!(pof->m_pcallbackFlash)(pof, EE_PROGRAM_ERROR, dw, (((DWORD)pba[dwSrc])<<8) |pof->m_pbMemoryMappedStartAddress[dw] )) return false;
					if(pof->m_pcallbackFlash!=NULL) if(!(pof->m_pcallbackFlash)(pof, EE_PROGRAM_END, 0, 0)) return false;
					return false;
				}
			} else {
				nCountProgramRetries=4;
				dwLastProgramAddress=dw;
			}

				// no ISRs should touch flash while we do the stuff
			__asm__ __volatile__ ( "pushf ; cli ");

			if(pof->m_fDetectedUsing28xxxConventions) {
				pof->m_pbMemoryMappedStartAddress[0x5555]=0x10;
				pof->m_pbMemoryMappedStartAddress[dw]=pba[dwSrc]; // erase the block containing the non 0xff guy
			} else {
				pof->m_pbMemoryMappedStartAddress[0x5555]=0xaa;
				pof->m_pbMemoryMappedStartAddress[0x2aaa]=0x55;
				pof->m_pbMemoryMappedStartAddress[0x5555]=0xa0;
				pof->m_pbMemoryMappedStartAddress[dw]=pba[dwSrc]; // erase the block containing the non 0xff guy
			}

				// wait out erase period
			{
				BYTE b=pof->m_pbMemoryMappedStartAddress[dw];  // waits until b6 is no longer toggling on each read
				while((pof->m_pbMemoryMappedStartAddress[dw]&0x40)!=(b&0x40)) b^=0x40;
			}

			__asm__ __volatile__ ( "popf ");

		}

		if((dw&0x3ff)==0) if(pof->m_pcallbackFlash!=NULL) if(!(pof->m_pcallbackFlash)(pof, EE_PROGRAM_UPDATE, dwSrc, pof->m_dwLengthUsedArea)) return false;

		dwLen--; dw++; dwSrc++;
	}

	if(pof->m_pcallbackFlash!=NULL) if(!(pof->m_pcallbackFlash)(pof, EE_PROGRAM_END, 0, 0)) return false;

		// verify

	if(pof->m_pcallbackFlash!=NULL) if(!(pof->m_pcallbackFlash)(pof, EE_VERIFY_START, 0, 0)) return false;

	dw=pof->m_dwStartOffset;
	dwLen=pof->m_dwLengthUsedArea;
	dwSrc=0;

	while(dwLen) {

		if(pof->m_pbMemoryMappedStartAddress[dw]!=pba[dwSrc]) { // verify error
			if(pof->m_pcallbackFlash!=NULL) if(!(pof->m_pcallbackFlash)(pof, EE_VERIFY_ERROR, dw, (((DWORD)pba[dwSrc])<<8) |pof->m_pbMemoryMappedStartAddress[dw])) return false;
			if(pof->m_pcallbackFlash!=NULL) if(!(pof->m_pcallbackFlash)(pof, EE_VERIFY_END, 0, 0)) return false;
			return false;
		}

		dwLen--; dw++; dwSrc++;
	}

	if(pof->m_pcallbackFlash!=NULL) if(!(pof->m_pcallbackFlash)(pof, EE_VERIFY_END, 0, 0)) return false;
	return true;
}
