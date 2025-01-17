///////////////////////////////////////////////
//
// **************************
// ** ENGLISH - 10/Jul/2017 **
//
// Project: libObfuscate v2.00
//
// This software is released under:
// * LGPL 3.0: "www.gnu.org/licenses/lgpl.html"
//
// You're free to copy, distribute and make commercial use
// of this software under the following conditions:
// * You cite the author and copyright owner: "www.embeddedsw.net"
// * You provide a link to the Homepage: "www.embeddedsw.net/libobfuscate.html"
//
///////////////////////////////////////////////

#include "stdafx.h"

#include "MultiBase.h"
#include "CSPRNG.h"

#include "Multi_data.h"

void BlockXor(BYTE *data,const BYTE *value)
{
	#if	DATA_BLOCK_SIZE==16
	*((DWORD *) (data+ 0))	^=*((DWORD *) (value+ 0));
	*((DWORD *) (data+ 4))	^=*((DWORD *) (value+ 4));
	*((DWORD *) (data+ 8))	^=*((DWORD *) (value+ 8));
	*((DWORD *) (data+12))	^=*((DWORD *) (value+12));
	#else
	#error
	#endif
}

void Multi_setkey(MULTI_DATA *pMd,const BYTE *iv,const BYTE *passw1,const BYTE *passw2,DWORD nonce)
{
	BYTE		passw[MAX_ALG][MAX_PASSW_SIZE];
	BYTE		usedMap[MAX_ALG];
	DWORD		index;

	memset(pMd,0,sizeof(MULTI_DATA));

	// CSPRNG <- Skein512(passw2 + nonce)
	CSPRNG_set_seed(&pMd->cd,SKEIN512_HASH,passw2,nonce);

	// IVs
	memcpy(pMd->iv[0],iv,MAX_ALG*DATA_BLOCK_SIZE);

	// passw[] <- KDF4 : random ( hash( passw1 + nonce) )
	for(index=0;index<MAX_HASH;index++)
		{
		DWORD	pIndex;

		CSPRNG_DATA	tmpCSPRNG;

		switch(index)
			{
			case 0:	CSPRNG_set_seed(&tmpCSPRNG,SHA512_HASH,passw1,nonce); break;
			case 1:	CSPRNG_set_seed(&tmpCSPRNG,GROSTL512_HASH,passw1,nonce); break;
			case 2:	CSPRNG_set_seed(&tmpCSPRNG,KECCAK512_HASH,passw1,nonce); break;
			case 3:	CSPRNG_set_seed(&tmpCSPRNG,SKEIN512_HASH,passw1,nonce); break;
			}

		for(pIndex=0;pIndex<(MAX_ALG/MAX_HASH);pIndex++)
			{
			DWORD	sIndex;

			for(sIndex=0;sIndex<MAX_PASSW_SIZE;sIndex++)
				{ passw[(index*(MAX_ALG/MAX_HASH))+pIndex][sIndex]=CSPRNG_get_byte(&tmpCSPRNG); }
			}
		}

	CSPRNG_array_init(&pMd->cd,MAX_ALG,usedMap);

	// subcipher setup
	Multi_single_setkey(&pMd->msd,ANUBIS_ALG,		passw[usedMap[ 0]]);
	Multi_single_setkey(&pMd->msd,CAMELLIA_ALG,		passw[usedMap[ 1]]);
	Multi_single_setkey(&pMd->msd,CAST256_ALG,		passw[usedMap[ 2]]);
	Multi_single_setkey(&pMd->msd,CLEFIA_ALG,		passw[usedMap[ 3]]);
	Multi_single_setkey(&pMd->msd,FROG_ALG,			passw[usedMap[ 4]]);
	Multi_single_setkey(&pMd->msd,HIEROCRYPT3_ALG,	passw[usedMap[ 5]]);
	Multi_single_setkey(&pMd->msd,IDEANXT128_ALG,	passw[usedMap[ 6]]);
	Multi_single_setkey(&pMd->msd,MARS_ALG,			passw[usedMap[ 7]]);
	Multi_single_setkey(&pMd->msd,RC6_ALG,			passw[usedMap[ 8]]);
	Multi_single_setkey(&pMd->msd,RIJNDAEL_ALG,		passw[usedMap[ 9]]);
	Multi_single_setkey(&pMd->msd,SAFERP_ALG,		passw[usedMap[10]]);
	Multi_single_setkey(&pMd->msd,SC2000_ALG,		passw[usedMap[11]]);
	Multi_single_setkey(&pMd->msd,SERPENT_ALG,		passw[usedMap[12]]);
	Multi_single_setkey(&pMd->msd,SPEED_ALG,		passw[usedMap[13]]);
	Multi_single_setkey(&pMd->msd,TWOFISH_ALG,		passw[usedMap[14]]);
	Multi_single_setkey(&pMd->msd,UNICORNA_ALG,		passw[usedMap[15]]);
}

#define	REFRESH_COUNTDOWN	100

OBFUNC_RETV Multi_CBC_encrypt(MULTI_DATA *pMd,const DWORD len,BYTE *buf,perc_callback_t pFunc,void *pDesc,test_callback_t tFunc,void *tDesc)
{
	DWORD	tLen=len;
	BYTE	lastPerc=0;
	WORD	refCount=REFRESH_COUNTDOWN;

	while(tLen>=DATA_BLOCK_SIZE)
		{
		BYTE	tmpIN[DATA_BLOCK_SIZE];
		BYTE	curAlg=CSPRNG_get_byte(&pMd->cd)%MAX_ALG;

		// OUT = encrypt( IN ^ IV_or_previous_out_block )
		memcpy(tmpIN,buf,DATA_BLOCK_SIZE);
		BlockXor(tmpIN,pMd->iv[curAlg]);

		Multi_ECB_single_encrypt(&pMd->msd,(ENUM_ALG) curAlg,tmpIN,buf);

		memcpy(pMd->iv[curAlg],buf,DATA_BLOCK_SIZE);

		buf+=DATA_BLOCK_SIZE;
		tLen-=DATA_BLOCK_SIZE;

		if(!refCount)
			{
			refCount=REFRESH_COUNTDOWN;

			if(pFunc)
				{
				BYTE	tmp=(BYTE) ((((float) (len-tLen))/((float) len))*((float) 100));
				if(tmp>lastPerc)
					{
					lastPerc=tmp;
					pFunc(pDesc,lastPerc);
					}
				}
			if(tFunc&&tFunc(tDesc))
				{ return(OBFUNC_STOP); }
			}

		refCount--;
		}

	return(OBFUNC_OK);
}

OBFUNC_RETV Multi_CBC_decrypt(MULTI_DATA *pMd,const DWORD len,BYTE *buf,perc_callback_t pFunc,void *pDesc,test_callback_t tFunc,void *tDesc)
{
	DWORD	tLen=len;
	BYTE	lastPerc=0;
	WORD	refCount=REFRESH_COUNTDOWN;

	while(tLen>=DATA_BLOCK_SIZE)
		{
		BYTE	tmpOUT[DATA_BLOCK_SIZE];
		BYTE	curAlg=CSPRNG_get_byte(&pMd->cd)%MAX_ALG;

		// OUT = decrypt( IN ) ^ IV_or_previous_in_block )
		Multi_ECB_single_decrypt(&pMd->msd,(ENUM_ALG) curAlg,buf,tmpOUT);

		BlockXor(tmpOUT,pMd->iv[curAlg]);
		memcpy(pMd->iv[curAlg],buf,DATA_BLOCK_SIZE);
		memcpy(buf,tmpOUT,DATA_BLOCK_SIZE);

		buf+=DATA_BLOCK_SIZE;
		tLen-=DATA_BLOCK_SIZE;

		if(!refCount)
			{
			refCount=REFRESH_COUNTDOWN;

			if(pFunc)
				{
				BYTE	tmp=(BYTE) ((((float) (len-tLen))/((float) len))*((float) 100));
				if(tmp>lastPerc)
					{
					lastPerc=tmp;
					pFunc(pDesc,lastPerc);
					}
				}
			if(tFunc&&tFunc(tDesc))
				{ return(OBFUNC_STOP); }
			}

		refCount--;
		}

	return(OBFUNC_OK);
}
