/* Copyright (c) Colorado School of Mines, 2000.*/
/* All rights reserved.                       */

/* SUFILTER: $Revision: 1.15 $ ; $Date: 1998/08/24 20:10:26 $        */


#include "su.h"
#include "segy.h"
#include "header.h"

/*********************** self documentation **********************/
char *sdoc[] = {
"									",
" SURFILTER - applies a zero-phase, sine-squared tapered filter		",
"									",
" suRfilter <stdin >stdout [optional parameters]         		",
"									",
" Required parameters:                                         		",
"       if dt is not set in header, then dt is mandatory        	",
"									",
" Optional parameters:							",
"       t0=0                    Starting of the  window to filter       ", 
"       t1=TMAX                 Endingg time                            ", 
"       f=f1,f2,...             array of filter frequencies(HZ) 	",
"       amps=a1,a2,...          array of filter amplitudes		",
"       dt = (from header)      time sampling interval (sec)        	",
"									",
" Defaults:f=.10*(nyquist),.15*(nyquist),.45*(nyquist),.50*(nyquist)	",
"                        (nyquist calculated internally)		",
"          amps=0.,1.,...,1.,0.  trapezoid-like bandpass filter		",
"									",
" Examples of filters:							",
" Bandpass:   sufilter <data f=10,20,40,50 | ...			",
" Bandreject: sufilter <data f=10,20,30,40 amps=1.,0.,0.,1. | ..	",
" Lowpass:    sufilter <data f=10,20,40,50 amps=1.,1.,0.,0. | ...	",
" Highpass:   sufilter <data f=10,20,40,50 amps=0.,0.,1.,1. | ...	",
" Notch:      sufilter <data f=10,12.5,35,50,60 amps=1.,.5,0.,.5,1. |..	",
"									",
" Notes: If ky1=0 tthe module filters all the traces in the file, What  ", 
"        the original sufilter did.					",
"        If t0 and t1 are not speciffied, the module filters the entire	",
"        trace                                                          ",
"									",
NULL};

/* Credits:
 *      CWP: John Stockwell, Jack Cohen,  
 *      ICTJA: Ramon Carbonell
 *
 * Possible optimization: Do assignments instead of crmuls where
 * filter is 0.0.
 *
 * Trace header fields accessed: ns, dt
 */
/**************** end self doc ***********************************/

/* Prototype of function used internally */
void polygonalFilter(float *f, float *amps,
			int npoly, int nfft, float dt, float *filter);

#define PIBY2   1.57079632679490
#define FRAC0   0.10    /* Ratio of default f1 to Nyquist */
#define FRAC1   0.15    /* Ratio of default f2 to Nyquist */
#define FRAC2   0.45    /* Ratio of default f3 to Nyquist */
#define FRAC3   0.50    /* Ratio of default f4 to Nyquist */
#define LOOKFAC 2       /* Look ahead factor for npfao    */
#define PFA_MAX 720720  /* Largest allowed nfft           */


segy tr,tmptr;

int
main(int argc, char **argv)
{
        register float *rt;     /* real trace                           */
        register complex *ct;   /* complex transformed trace            */
        float *filter;          /* filter array                         */
        float *f;               /* array of filter frequencies		*/
        int npoly,is0,ntw,is1;  /* .... sizes of f and intfr	        */
        float *amps;            /* array of amplitude values		*/
        int namps;              /* .... size of amps                    */
        int icount,ifs,iamps;   /* loop counting variables              */
        float dt,t0,t1;         /* sample spacing                       */
        float nyq;              /* nyquist frequency                    */
        int nt;         /* number of points on input trace      */
        int nfft;               /* number of points for fft trace       */
        int nf;             /* number of frequencies (incl Nyq)     */
	cwp_Bool seismic;	/* is this seismic data?		*/

        
        /* Initialize */
        initargs(argc, argv);
        requestdoc(1);


        /* Get info from first trace */ 
        if (!gettr(&tr))  err("can't get first trace");
	seismic = ISSEISMIC(tr.trid);
		
	if (!seismic)
		err("input is not seismic data, trid=%d", tr.trid);
        nt = tr.ns;
	if (!getparfloat("dt", &dt))	dt = ((double) tr.dt)/1000000.0;
        if (!dt) err("dt field is zero and not getparred");
	if (!getparfloat("t0", &t0))	t0 = 0.0;
	if (!getparfloat("t1", &t1))	t1 = nt*dt;
        is0=t0/dt;
        is1=t1/dt;
        ntw=is1-is0;
        nyq = 0.5/dt;

        /* Set up FFT parameters */
        nfft = npfaro(nt, LOOKFAC * nt);
        if (nfft >= SU_NFLTS || nfft >= PFA_MAX)
                err("Padded nt=%d -- too big", nfft);

        nf = nfft/2 + 1;

        /* Get frequencies that define the filter */
        if ((npoly = countparval("f"))!=0) {
                f = ealloc1float(npoly);
                getparfloat("f",f);
        } else {
                npoly = 4;
                f = ealloc1float(npoly);

                f[0] = FRAC0 * nyq;
                f[1] = FRAC1 * nyq;
                f[2] = FRAC2 * nyq;
                f[3] = FRAC3 * nyq;
        }

	/* Check f values */
	if(npoly < 2) warn("Only %d value defining filter",npoly);
        for(ifs=0; ifs < npoly-1; ++ifs)
		if(f[ifs] < 0.0 || f[ifs] > f[ifs+1])
                                err("Bad filter parameters");
	
	/* Get filter amplitude values*/
        if ((namps = countparval("amps"))!=0) {
                amps = ealloc1float(namps);
                getparfloat("amps",amps);
        } else {
                namps = npoly;
                amps = ealloc1float(namps);

		/* default is a trapezoidal bandpass filter */
		for(iamps=0; iamps<namps; ++iamps)
               		amps[iamps]=1.;
		
		amps[0]=0.; amps[namps-1]=0.;
        }
	if (!(namps==npoly)) 
		err("number of f values must = number of amps values");
        
        /* Check amps values */
        for(iamps = 0, icount=0; iamps < namps ; ++iamps) {
		if( amps[iamps] > 0. ) ++icount;
                if( amps[iamps] < 0.) err("amp values must be positive");
        }
        if (icount==0) err("All amps values are zero");
        for(iamps = 0, icount=0; iamps < namps-1 ; ++iamps) {
			if(!(amps[iamps]==amps[iamps+1])) ++icount;
	}
        if (icount==0) warn("All amps values are the same");


        /* Allocate fft arrays */
        rt   = ealloc1float(nfft);
        ct   = ealloc1complex(nf);
        filter = ealloc1float(nf);

	/* Build the polygonal filter filter[]*/
	polygonalFilter(f,amps,npoly,nfft,dt,filter);

        /* Main loop over traces */
        do {
              if ( !tr.ifflg ) {
                    register int i;
                    /* Load trace into rt (zero-padded) */
                    for (i=0;i<nt;++i) tmptr.data[i]=0.0;
                    for (i=is0;i<is1;++i) tmptr.data[i]=tr.data[i];
                    memcpy((void *) rt, (const void *) tmptr.data, nt*FSIZE);
                    memset((void *) (rt + nt), (int) '\0' , (nfft-nt)*FSIZE);

                    /* FFT, filter, inverse FFT */
                    pfarc(1, nfft, rt, ct);
                    for (i = 0; i < nf; ++i)  ct[i] = crmul(ct[i], filter[i]);
                    pfacr(-1, nfft, ct, rt);

                    /* Load traces back in, recall filter had nfft factor */
                    for (i = 0; i < nt; ++i)  tmptr.data[i] = rt[i];
                    for (i = is0; i < is1; ++i)  tr.data[i] = tmptr.data[i];
             }
             puttr(&tr);
        } while (gettr(&tr));
       return EXIT_SUCCESS;
}


void polygonalFilter(float *f, float *amps, int npoly,
				int nfft, float dt, float *filter)
/*************************************************************************
polygonalFilter -- polygonal filter with sin^2 tapering
**************************************************************************
Input:
f		array[npoly] of frequencies defining the filter
amps		array[npoly] of amplitude values
npoly		size of input f and amps arrays
dt		time sampling interval
nfft		number of points in the fft

Output:
filter		array[nfft] filter values
**************************************************************************
Notes: Filter is to be applied in the frequency domain
**************************************************************************
Author:  CWP: John Stockwell   1992
*************************************************************************/
#define PIBY2   1.57079632679490
{
        int *intfr;             /* .... integerizations of f		*/
        int icount,ifs;		/* loop counting variables              */
	int taper=0;		/* flag counter				*/
        int nf;                 /* number of frequencies (incl Nyq)     */
        int nfm1;               /* nf-1                                 */
        float onfft;            /* reciprocal of nfft                   */
        float df;               /* frequency spacing (from dt)          */

        
	intfr=alloc1int(npoly);

        nf = nfft/2 + 1;
        nfm1 = nf - 1;
        onfft = 1.0 / nfft;

        /* Compute array of integerized frequencies that define the filter*/
        df = onfft / dt;
        for(ifs=0; ifs < npoly ; ++ifs) {
                intfr[ifs] = NINT(f[ifs]/df);
                if (intfr[ifs] > nfm1) intfr[ifs] = nfm1;
        }

	/* Build filter, with scale, and taper specified by amps[] values*/
	/* Do low frequency end first*/
	for(icount=0; icount < intfr[0] ; ++icount) 
		filter[icount] = amps[0] * onfft;

	/* now do the middle frequencies */
	for(ifs=0 ; ifs<npoly-1 ; ++ifs){
	   if(amps[ifs] < amps[ifs+1]) {	
		++taper;
		for(icount=intfr[ifs]; icount<=intfr[ifs+1]; ++icount) {
		    float c = PIBY2 / (intfr[ifs+1] - intfr[ifs] + 2);
		    float s = sin(c*(icount - intfr[ifs] + 1));
		    float adiff = amps[ifs+1] - amps[ifs];
		    filter[icount] = (amps[ifs] + adiff*s*s) * onfft;
		}
	   } else if (amps[ifs] > amps[ifs+1]) {	
		++taper;
		for(icount=intfr[ifs]; icount<=intfr[ifs+1]; ++icount) {
			   float c = PIBY2 / (intfr[ifs+1] - intfr[ifs] + 2);
                	   float s = sin(c*(intfr[ifs+1] - icount + 1));
			   float adiff = amps[ifs] - amps[ifs+1];
                	   filter[icount] = (amps[ifs+1] + adiff*s*s) * onfft;
		  }
	   } else 
		if(!(taper)){
		for(icount=intfr[ifs]; icount <= intfr[ifs+1]; ++icount)
		   	   filter[icount] = amps[ifs] * onfft;
		} else {
		for(icount=intfr[ifs]+1; icount <= intfr[ifs+1]; ++icount)
		   	   filter[icount] = amps[ifs] * onfft;
		}
	}

	/* finally do the high frequency end */
	for(icount=intfr[npoly-1]+1; icount<nf; ++icount){
		filter[icount] = amps[npoly-1] * onfft;
	}

}
