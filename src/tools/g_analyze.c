/*
 * $Id$
 * 
 *       This source code is part of
 * 
 *        G   R   O   M   A   C   S
 * 
 * GROningen MAchine for Chemical Simulations
 * 
 *               VERSION 2.0
 * 
 * Copyright (c) 1991-1999
 * BIOSON Research Institute, Dept. of Biophysical Chemistry
 * University of Groningen, The Netherlands
 * 
 * Please refer to:
 * GROMACS: A message-passing parallel molecular dynamics implementation
 * H.J.C. Berendsen, D. van der Spoel and R. van Drunen
 * Comp. Phys. Comm. 91, 43-56 (1995)
 * 
 * Also check out our WWW page:
 * http://md.chem.rug.nl/~gmx
 * or e-mail to:
 * gromacs@chem.rug.nl
 * 
 * And Hey:
 * GRowing Old MAkes el Chrono Sweat
 */
static char *SRCID_g_analyze_c = "$Id$";

#include <math.h>
#include <string.h>
#include "statutil.h"
#include "sysstuff.h"
#include "typedefs.h"
#include "smalloc.h"
#include "macros.h"
#include "fatal.h"
#include "vec.h"
#include "copyrite.h"
#include "futil.h"
#include "statutil.h"
#include "txtdump.h"
#include "gstat.h"
#include "xvgr.h"

static real **read_val(char *fn,bool bHaveT,bool bTB,real tb,bool bTE,real te,
		       int nsets_in,int *nset,int *nval,real *t0,real *dt,
		       int linelen)
{
  FILE   *fp;
  static int  llmax=0;
  static char *line0=NULL;
  char   *line;
  int    a,narg,n,sin,set,nchar;
  double dbl,tend=0;
  bool   bEndOfSet,bTimeInRange;
  real   **val;

  if (linelen > llmax) {
    llmax = linelen;
    srenew(line0,llmax);
  }

  val = NULL;
  *t0 = 0;
  *dt = 0;
  fp  = ffopen(fn,"r");
  for(sin=0; sin<nsets_in; sin++) {
    if (nsets_in == 1)
      narg = 0;
    else 
      narg = bHaveT ? 2 : 1;
    n = 0;
    bEndOfSet = FALSE;
    while (!bEndOfSet && fgets(line0,linelen,fp)) {
      line = line0;
      bEndOfSet = (line[0] == '&');
      if ((line[0] != '#') && (line[0] != '@') && !bEndOfSet) {
	a = 0;
	bTimeInRange = TRUE;
	while ((a<narg || (nsets_in==1 && n==0)) && 
	       line[0]!='\n' && sscanf(line,"%lf%n",&dbl,&nchar)
	       && bTimeInRange) {
	  /* Use set=-1 as the time "set" */
	  if (sin) {
	    if (!bHaveT || (a>0))
	      set = sin;
	    else
	      set = -1;
	  } else {
	    if (!bHaveT)
	      set = a;
	    else
	      set = a-1;
	  }
	  if (set==-1 && ((bTB && dbl<tb) || (bTE && dbl>te)))
	    bTimeInRange = FALSE;
	    
	  if (bTimeInRange) {
	    if (n==0) {
	      if (nsets_in == 1)
		narg++;
	      if (set == -1)
		*t0 = dbl;
	      else {
		*nset = set+1;
		srenew(val,*nset);
		val[set] = NULL;
	      }
	    }
	    if (set == -1)
	      tend = dbl;
	    else {
	      if (n % 100 == 0)
	      srenew(val[set],n+100);
	      val[set][n] = (real)dbl;
	    }
	  }
	  a++;
	  line += nchar;
	}
	if (bTimeInRange) {
	  n++;
	  if (a != narg)
	    fprintf(stderr,"Invalid line in %s: '%s'\n",fn,line0);
	}
      }
    }
    if (sin==0) {
      *nval = n;
      if (!bHaveT)
	*dt = 1;
      else 
	if (n > 1)
	  *dt = (real)(tend-*t0)/(n-1.0);
    } else {
      if (n < *nval) {
	fprintf(stderr,"Set %d is shorter (%d) than the previous set (%d)\n",
		sin+1,n,*nval);
	*nval = n;
	fprintf(stderr,"Will use only the first %d points of every set\n",
		*nval);
      }
    }
  }
  fclose(fp);
  
  return val;
}

void histogram(char *distfile,real binwidth,int n, int nset, real **val)
{
  FILE *fp;
  int  i,s;
  real min,max;
  int  nbin;
  real *histo;

  min=val[0][0];
  max=val[0][0];
  for(s=0; s<nset; s++)
    for(i=0; i<n; i++)
      if (val[s][i] < min)
	min = val[s][i];
      else if (val[s][i] > max)
	max = val[s][i];
  
  if (-min > max)
    max = -min;
  nbin = (int)(max/binwidth)+1;
  fprintf(stderr,"Making distributions with %d bins\n",2*nbin+1);
  snew(histo,2*nbin+1);
  fp = xvgropen(distfile,"Distribution","","");
  for(s=0; s<nset; s++) {
    for(i=0; i<2*nbin+1; i++)
      histo[i] = 0;
    for(i=0; i<n; i++)
      histo[nbin+(int)(floor(val[s][i]/binwidth+0.5))]++;
    for(i=0; i<2*nbin+1; i++)
      fprintf(fp," %g  %g\n",(i-nbin)*binwidth,(real)histo[i]/(n*binwidth));
    if (s<nset-1)
      fprintf(fp,"&\n");
  }
  fclose(fp);
}

static int real_comp(const void *a,const void *b)
{
  real dif = *(real *)a - *(real *)b;

  if (dif < 0)
    return -1;
  else if (dif > 0)
    return 1;
  else
    return 0;
}

static void average(char *avfile,char **avbar_opt,
		    int n, int nset,real **val,real t0,real dt)
{
  FILE   *fp;
  int    i,s,edge=0;
  double av,var,err;
  real   *tmp=NULL;
  char   c;
  
  c = avbar_opt[0][0];

  fp = ffopen(avfile,"w");
  if ((c == 'e') && (nset == 1))
    c = 'n';
  if (c != 'n') {
    if (c == '9') {
      snew(tmp,nset);
      fprintf(fp,"@TYPE xydydy\n");
      edge = (int)(nset*0.05+0.5);
      fprintf(stdout,"Errorbars: discarding %d points on both sides: %d%%"
	      " interval\n",edge,(int)(100*(nset-2*edge)/nset+0.5));
    } else
      fprintf(fp,"@TYPE xydy\n");
  }
  
  for(i=0; i<n; i++) {
    av = 0;
    for(s=0; s<nset; s++)
      av += val[s][i];
    av /= nset;
    fprintf(fp," %g %g",t0+dt*i,av);
    var = 0;
    if (c != 'n') {
      if (c == '9') {
	for(s=0; s<nset; s++)
	  tmp[s] = val[s][i];
	qsort(tmp,nset,sizeof(tmp[0]),real_comp);
	fprintf(fp," %g %g",tmp[nset-1-edge]-av,av-tmp[edge]);
      } else {
	for(s=0; s<nset; s++)
	  var += sqr(val[s][i]-av);
	if (c == 's')
	  err = sqrt(var/nset);
	else
	  err = sqrt(var/(nset*(nset-1)));
	fprintf(fp," %g",err);
      }
    }
    fprintf(fp,"\n");
  }
  fclose(fp);
  
  if (c == '9')
    sfree(tmp);
}
static real anal_ee_inf(real *parm,real T)
{
  return sqrt(parm[1]*2*parm[0]/T+parm[3]*2*parm[2]/T);
}

static real anal_ee(real *parm,real T,real t)
{
  real e1,e2;

  if (parm[0])
    e1 = exp(-t/parm[0]);
  else
    e1 = 1;
  if (parm[2])
    e2 = exp(-t/parm[2]);
  else
    e2 = 1;

  return sqrt(parm[1]*2*parm[0]/T*((e1 - 1)*parm[0]/t + 1) +
	      parm[3]*2*parm[2]/T*((e2 - 1)*parm[2]/t + 1));
}

static void estimate_error(char *eefile,int nb_min,int resol,int n,int nset,
			   double *av,double *sig,real **val,real dt,
			   bool bFitAc)
{
  FILE   *fp;
  int    bs,prev_bs,nbs,nb;
  real   spacing,nbr;
  int    s,i,j;
  double blav,var;
  char   **leg;
  real   *tbs,*ybs,rtmp,*fitsig,fitparm[4];

  fp = xvgropen(eefile,"Error estimates","Block size (time)","Error estimate");
  fprintf(fp,
	  "@ subtitle \"using block averaging, total time %g (%d points)\"\n",
	  n*dt,n);
  snew(leg,2*nset);
  xvgr_legend(fp,2*nset,leg);
  sfree(leg);

  spacing = pow(2,1.0/resol);
  snew(tbs,n);
  snew(ybs,n);
  snew(fitsig,n);
  for(s=0; s<nset; s++) {
    nbs = 0;
    prev_bs = 0;
    nbr = nb_min;
    while (nbr <= n) {
      bs = n/(int)nbr;
      if (bs != prev_bs) {
	nb = n/bs;
	var = 0;
	for(i=0; i<nb; i++) {
	  blav=0;
	  for (j=0; j<bs; j++)
	    blav += val[s][bs*i+j];
	  var += sqr(av[s] - blav/bs);
	}
	tbs[nbs] = bs*dt;
	ybs[nbs] = sqrt(var/(nb*(nb-1.0))*(n*dt))/sig[s];
	nbs++;
      }
      nbr *= spacing;
      nb = (int)(nbr+0.5);
      prev_bs = bs;
    }

    for(i=0; i<nbs/2; i++) {
      rtmp         = tbs[i];
      tbs[i]       = tbs[nbs-1-i];
      tbs[nbs-1-i] = rtmp;
      rtmp         = ybs[i];
      ybs[i]       = ybs[nbs-1-i];
      ybs[nbs-1-i] = rtmp;
    }
    for(i=0; i<nbs; i++)
      fitsig[i] = sqrt(tbs[i]);

    fitparm[0] = 0.002*n*dt;
    fitparm[1] = 0.95;
    fitparm[2] = 0.2*n*dt;
    do_lmfit(nbs,ybs,fitsig,0,tbs,0,dt*n,bDebugMode(),effnERREST,fitparm,0);
    if (fitparm[0]<0 || fitparm[2]<0 || fitparm[1]<0 || fitparm[1]>1) {
      fprintf(stderr,"Will use a single exponential fit for set %d\n",s+1);
      fitparm[0] = n*dt*0.002;
      fitparm[1] = 1;
      fitparm[2] = 0;
      do_lmfit(nbs,ybs,fitsig,0,tbs,0,dt*n,bDebugMode(),effnERREST,fitparm,6);
    }
    fitparm[3] = 1-fitparm[1];
    fprintf(stdout,"Set %3d:  err.est. %g  a %g  tau1 %g  tau2 %g\n",
	    s+1,sig[s]*anal_ee_inf(fitparm,n*dt),
	    fitparm[1],fitparm[0],fitparm[2]);
    fprintf(fp,"@ legend string %d \"av %f\"\n",2*s,av[s]);
    fprintf(fp,"@ legend string %d \"ee %6g\"\n",
	    2*s+1,sig[s]*anal_ee_inf(fitparm,n*dt));
    for(i=0; i<nbs; i++)
      fprintf(fp,"%g %g %g\n",tbs[i],sig[s]/sqrt(n*dt)*ybs[i],
	      sig[s]/sqrt(n*dt)*fit_function(effnERREST,fitparm,tbs[i]));

    if (bFitAc) {
      real *ac,ac_fit[4];
      
      snew(ac,n);
      for(i=0; i<n; i++) {
	ac[i] = val[s][i] - av[s];
	if (i > 0)
	  fitsig[i] = sqrt(i);
	else
	  fitsig[i] = 1;
      }
      low_do_autocorr(NULL,NULL,n,1,-1,&ac,
		      dt,eacNormal,1,FALSE,TRUE,TRUE,
		      FALSE,0,0,
		      effnEXP3,0);
      
      ac_fit[0] = 0.002*n*dt;
      ac_fit[1] = 0.95;
      ac_fit[2] = 0.2*n*dt;
      do_lmfit((n+1)/nb_min,ac,fitsig,dt,0,0,dt*(n+1)*0.25,
              bDebugMode(),effnEXP3,ac_fit,0);
      ac_fit[3] = 1 - ac_fit[1];

      fprintf(stdout,"Set %3d:  ac erest %g  a %g  tau1 %g  tau2 %g\n",
	    s+1,sig[s]*anal_ee_inf(ac_fit,n*dt),
	    ac_fit[1],ac_fit[0],ac_fit[2]);

      fprintf(fp,"&\n");
      for(i=0; i<nbs; i++)
	fprintf(fp,"%g %g\n",tbs[i],
		sig[s]/sqrt(n*dt)*fit_function(effnERREST,ac_fit,tbs[i]));

      sfree(ac);
    }
    if (s < nset-1)
      fprintf(fp,"&\n");
  }
  sfree(fitsig);
  sfree(ybs);
  sfree(tbs);
  fclose(fp);
}

int main(int argc,char *argv[])
{
  static char *desc[] = {
    "g_analyze reads an ascii file and analyzes data sets.",
    "A line in the input file may start with a time",
    "(see option [TT]-time[tt]) and any number of y values may follow.",
    "Multiple sets can also be",
    "read when they are seperated by & (option [TT]-n[tt]),",
    "in this case only one y value is read from each line.",
    "All lines starting with # and @ are skipped.",
    "All analyses can also be done for the derivative of a set",
    "(option [TT]-d[tt]).[PAR]",
    "g_analyze always shows the average and standard deviation of each",
    "set. For each set it also shows the relative deviation of the third",
    "and forth cumulant from those of a Gaussian distribution with the same",
    "standard deviation.[PAR]",
    "Option [TT]-ac[tt] produces the autocorrelation function(s).[PAR]",
    "Option [TT]-msd[tt] produces the mean square displacement(s).[PAR]",
    "Option [TT]-dist[tt] produces distribution plot(s).[PAR]",
    "Option [TT]-av[tt] produces the average over the sets.",
    "Error bars can be added with the option [TT]-errbar[tt].",
    "The errorbars can represent the standard deviation, the error",
    "(assuming the points are independent) or the interval containing",
    "90% of the points, by discarding 5% of the points at the top and",
    "the bottom.[PAR]",
    "Option [TT]-ee[tt] produces error estimates using block averaging.",
    "A set is divided in a number of blocks and averages are calculated for",
    "each block. The error for the total average is calculated from",
    "the variance between averages of the m blocks B_i as follows:",
    "error^2 = Sum (B_i - <B>)^2 / (m*(m-1)).",
    "These errors are plotted as a function of the block size.",
    "Also an analytical block average curve is plotted, assuming",
    "that the autocorrelation is a sum of two exponentials.",
    "The analytical curve for the block average BA is:[BR]",
    "BA(t) = sigma sqrt(2/T (  a   (tau1 ((exp(-t/tau1) - 1) tau1/t + 1)) +[BR]",
    "                        (1-a) (tau2 ((exp(-t/tau2) - 1) tau2/t + 1)))),[BR]"
    "where T is the total time.",
    "a, tau1 and tau2 are obtained by fitting BA(t) to the calculated block",
    "average.",
    "When the actual block average is very close to the analytical curve,",
    "the error is sigma*sqrt(2/T (a tau1 + (1-a) tau2))."
  };
  static real tb=-1,te=-1,frac=0.5,binwidth=0.1;
  static bool bHaveT=TRUE,bDer=FALSE,bSubAv=FALSE,bAverCorr=FALSE;
  static bool bEeFitAc=FALSE;
  static int  linelen=4096,nsets_in=1,d=1,nb_min=4,resol=10;

  static char *avbar_opt[] = { NULL, "none", "stddev", "error", "90", NULL };

  t_pargs pa[] = {
    { "-linelen", FALSE, etINT, {&linelen},
      "HIDDENMaximum input line length" },
    { "-time", FALSE, etBOOL, {&bHaveT},
      "Expect a time in the input" },
    { "-b", FALSE, etREAL, {&tb},
      "First time to read from set" },
    { "-e", FALSE, etREAL, {&te},
      "Last time to read from set" },
    { "-n", FALSE, etINT, {&nsets_in},
      "Read # sets seperated by &" },
    { "-d", FALSE, etBOOL, {&bDer},
	"Use the derivative" },
    { "-dp",  FALSE, etINT, {&d}, 
      "HIDDENThe derivative is the difference over # points" },
    { "-bw", FALSE, etREAL, {&binwidth},
      "Binwidth for the distribution" },
    { "-errbar", FALSE, etENUM, {&avbar_opt},
      "Error bars for -av" },
    { "-nbmin", FALSE, etINT, {&nb_min},
      "HIDDENMinimum number of blocks for block averaging" },
    { "-resol", FALSE, etINT, {&resol},
      "HIDDENResolution for the block averaging, block size increases with"
    " a factor 2^(1/#)" },
    { "-eefitac", FALSE, etBOOL, {&bEeFitAc},
      "HIDDENAlso plot analytical block average using a autocorrelation fit" },
    { "-subav", FALSE, etBOOL, {&bSubAv},
      "Subtract the average before autocorrelating" },
    { "-oneacf", FALSE, etBOOL, {&bAverCorr},
      "Calculate one ACF over all sets" }
  };
#define NPA asize(pa)

  FILE     *out;
  int      n,nlast,s,nset,i,t=0;
  real     **val,t0,dt,tot,error;
  double   *av,*sig,cum1,cum2,cum3,cum4,db;
  char     *acfile,*msdfile,*distfile,*avfile,*eefile;
  
  t_filenm fnm[] = { 
    { efXVG, "-f",    "graph",    ffREAD   },
    { efXVG, "-ac",   "autocorr", ffOPTWR  },
    { efXVG, "-msd",  "msd",      ffOPTWR  },
    { efXVG, "-dist", "distr",    ffOPTWR  },
    { efXVG, "-av",   "average",  ffOPTWR  },
    { efXVG, "-ee",   "errest",   ffOPTWR  }
  }; 
#define NFILE asize(fnm) 

  int     npargs;
  t_pargs *ppa;

  npargs = asize(pa); 
  ppa    = add_acf_pargs(&npargs,pa);
  
  CopyRight(stderr,argv[0]); 
  parse_common_args(&argc,argv,PCA_CAN_VIEW,TRUE,
		    NFILE,fnm,npargs,ppa,asize(desc),desc,0,NULL); 

  acfile   = opt2fn_null("-ac",NFILE,fnm);
  msdfile  = opt2fn_null("-msd",NFILE,fnm);
  distfile = opt2fn_null("-dist",NFILE,fnm);
  avfile   = opt2fn_null("-av",NFILE,fnm);
  eefile   = opt2fn_null("-ee",NFILE,fnm);

  val=read_val(opt2fn("-f",NFILE,fnm),bHaveT,
	       opt2parg_bSet("-b",npargs,ppa),tb,
	       opt2parg_bSet("-e",npargs,ppa),te,
	       nsets_in,&nset,&n,&t0,&dt,linelen);
  fprintf(stdout,"Read %d sets of %d points, dt = %g\n\n",nset,n,dt);
  if (bDer) {
    fprintf(stdout,"Calculating the derivative as (f[i+%d]-f[i])/(%d*dt)\n\n",
	    d,d);
    n -= d;
    for(s=0; s<nset; s++)
      for(i=0; (i<n); i++)
	val[s][i] = (val[s][i+d]-val[s][i])/(d*dt);
  }

  fprintf(stdout,"                                      std. dev.    relative deviation of\n");
  fprintf(stdout,"                       standard       ---------   cumulants from those of\n");
  fprintf(stdout,"set      average       deviation      sqrt(n-1)   a Gaussian distribition\n");
  fprintf(stdout,"                                                      cum. 3   cum. 4\n");
  snew(av,nset);
  snew(sig,nset);
  for(s=0; (s<nset); s++) {
    cum1 = 0;
    cum2 = 0;
    cum3 = 0;
    cum4 = 0;
    for(i=0; (i<n); i++)
      cum1 += val[s][i];
    cum1 /= n;
    for(i=0; (i<n); i++) {
      db = val[s][i]-cum1;
      cum2 += db*db;
      cum3 += db*db*db;
      cum4 += db*db*db*db;
    }
    cum2  /= n;
    cum3  /= n;
    cum4  /= n;
    av[s]  = cum1;
    sig[s] = sqrt(cum2);
    if (n > 1)
      error = sqrt(cum2/(n-1));
    else
      error = 0;
    fprintf(stdout,"%3d  %13.6e   %12.6e   %12.6e      %6.3f   %6.3f\n",
	    s+1,av[s],sig[s],error,
	    sig[s] ? cum3/(sig[s]*sig[s]*sig[s]*sqrt(8/M_PI)) : 0,
	    sig[s] ? cum4/(sig[s]*sig[s]*sig[s]*sig[s]*3)-1 : 0); 
  }
  fprintf(stdout,"\n");

  if (msdfile) {
    out=xvgropen(msdfile,"Mean square displacement",
		 "time (ps)","MSD (nm\\S2\\N)");
    nlast = (int)(n*frac);
    for(s=0; s<nset; s++) {
      for(t=0; t<=nlast; t++) {
	if (t % 100 == 0)
	  fprintf(stderr,"\r%d",t);
	tot=0;
	for(i=0; i<n-t; i++)
	  tot += sqr(val[s][i]-val[s][i+t]); 
	tot /= (real)(n-t);
	fprintf(out," %g %8g\n",dt*t,tot);
      }
      if (s<nset-1)
	fprintf(out,"&\n");
    }
    fclose(out);
    fprintf(stderr,"\r%d, time=%g\n",t-1,(t-1)*dt);
    do_view(msdfile, NULL);
  }
  
  if (distfile) {
    histogram(distfile,binwidth,n,nset,val);
    do_view(distfile, NULL);
  }
  if (avfile) {
    average(avfile,avbar_opt,n,nset,val,t0,dt);
    do_view(avfile, NULL);
  }
  if (eefile) {
    estimate_error(eefile,nb_min,resol,n,nset,av,sig,val,dt,bEeFitAc);
    do_view(eefile, NULL);
  }
  if (acfile) {
    if (bSubAv) 
      for(s=0; s<nset; s++)
	for(i=0; i<n; i++)
	  val[s][i] -= av[s];
    do_autocorr(acfile,"Autocorrelation",n,nset,val,dt,
		eacNormal,bAverCorr);
    do_view(acfile, NULL);
  }

  thanx(stderr);

  return 0;
}
  
