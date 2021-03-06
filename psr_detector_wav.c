#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>

#define DEFAULT_BINS 250

int
main (int argc, char **argv)
{
	double thebuffer[DEFAULT_BINS*5];
	double theotherbuffer[DEFAULT_BINS*5];
	int bincnts[DEFAULT_BINS*5];
	int numbins = DEFAULT_BINS;
	double prate =0.0;
	double sdt = 0.0;
	double tpb = 0.0;
	double srate = 0.0;
	double thresh;
	short sample;
	double minv = 0.0;
	double maxv = 0.0;
	int indx = 0;
	double segt = 0.0;
	long long totsamps = 0LL;
	long long skipped = 0LL;
	int i;
	unsigned long offset;
	unsigned long duration;
	double add;
	int opt;
	char *fn;
	FILE *fp;
	double aval = -5e9;
	double alpha = 0.05;
	double beta =  1.0-alpha;
	int avcnt = 0;
	double lp = 0.0;
	int logmode = 0;
		
	
	if (argc < 2)
	{
		fprintf (stderr, "Usage: do_psr -p <pulserate> -s <samprate> -o <offset> -d <duration> -n <numbins> <infile>\n");
		exit (1);
	}
	
	/*
	 * Set defaults
	 */
	prate = -1;
	srate = -1;
	offset = 0L;
	duration = 3600L;
	numbins = 250;
	add = 0.0;
	thresh = 5.0;
	
	while ((opt = getopt(argc, argv, "p:n:d:s:o:a:t:l")) != -1)
	{
		switch(opt)
		{
		case 'a':
			add = atof(optarg);
			break;
			
		case 'p':
		    prate = atof(optarg);
		    break;
		case 'n':
		    numbins = atoi(optarg);
		    if (numbins > (5*DEFAULT_BINS))
		    {
				fprintf (stderr, "Numbins exceeds the maximum of %d\n", (5*DEFAULT_BINS));
				exit (1);
			}
		    break;
		case 'd':
		    duration = atol(optarg);
		    break;
		case 's':
		    srate = atof(optarg);
		    break;
		case 'o':
		    offset = atol(optarg);
			break;
		case 't':
			thresh = atof(optarg);
			break;
	    case 'l':
			logmode = 1;
			break;
			
	    default:
	        fprintf (stderr, "Unknown option: '%c'\n", opt);
	        exit (1);
	    }
	 }
	 
	 /*
	  * Make sure that -p and -s have both been specified, and are valid
	  */
	 if (prate < 0.0)
	 {
		 fprintf (stderr, "-p must be specified, and must be > 0\n");
		 exit (1);
	 }
	 if (prate > 50.0)
	 {
		 fprintf (stderr, "-p specifies an unusually-high expected pulse rate\n");
	 }
	 
	 
	 if (srate < 0.0)
	 {
		 fprintf (stderr, "-s must be specified, and must be > 0\n");
		 exit (1);
	 }
	 if (srate < 100.0)
	 {
		 fprintf (stderr, "Warning: -s specifies an unusually-low sample rate\n");
     }
     
     fprintf (stderr, "Running with: prate %f srate %f bins %d\n", prate, srate, numbins);
     
     /*
      * Open input file
      */
	 fn = argv[optind];
	 fp = fopen (fn, "r");
	 if (fp == NULL)
	 {
		 perror ("Cannot open input file");
		 exit (1);
	 }
	 
	/*
	 * Offset/duration are given in seconds--convert to samples
	 */
	offset *= srate*sizeof(sample);
	duration *= srate*sizeof(sample);
	
	/*
	 * Calculate folding values from input parameters
	 */
	
	/*
	 * Time-per-bin
	 */
	tpb = (1.0/prate)/(double)numbins;
	
	fprintf (stderr, "Time-per-bin: %f\n", tpb);
	
	/*
	 * Dt produced by sample-rate
	 */
	sdt = 1.0/srate;
	
	fprintf (stderr, "Dt: %f\n", sdt);

	
	/*
	 * Zero-out out buffers
	 */
	memset ((void *)thebuffer, 0, sizeof(thebuffer));
	memset ((void *)bincnts, 0, sizeof(bincnts));
	
	/*
	 * Get to desired offset  (+16 to skip WAV header)
	 */
	if (fseek (fp, offset+16, SEEK_SET) == -1)
	{
		perror ("Seek failed");
	}
	
	/*
	 * We read one 16-bit sample at a time, and process accordingly.
	 * This is reasonably efficient, since stdio takes care of buffering
	 *   for us.
	 */

	while (fread(&sample, sizeof(sample), 1, fp) > 0)
	{
		double ds;
		
		/*
		 * Housekeeping on number of samples
		 */
		totsamps ++;
		
		/*
		 * We're done, even though we haven't reached EOF
		 */
		if (totsamps > duration)
		{
			break;
		}
		
		/*
		 * Scale sample into something "reasonable" for a double
		 */
		ds = (double)sample / (double)16384.5;
		
		{
			/*
			 * Implement high-pass filter
			 */
			alpha = 1.0 / (srate*30.0);
			lp = (alpha * ds) + ((1.0-alpha) *lp);
			ds -= lp;
			ds += add;
		}
		/*
		 * Add into accumulator buffer
		 * 
		 * We are "free and easy" for the first couple of minutes, and then we've established a baseline
		 */
		if (totsamps < (srate * 600))
		{
		    thebuffer[indx] += ds;
		    bincnts[indx] += 1;
		    if (aval < -1e9)
		    {
				aval = ds;
				fprintf (stderr, "Initializing aval\n");
				avcnt++;
			}
			else
			{
				aval += ds;
				avcnt++;
			}
		}
		/*
		 * Once a baseline is established, we clip based on this baseline
		 */
		else if (thresh > 0.0)
		{
			if (avcnt > 0)
			{
				aval /= (double) avcnt;
				avcnt = -1;
			}
			/*
			 * If the deviation from the local-average isn't too large, add this sample
			 */
			if (fabs(ds) < (fabs(aval) * thresh))
			{
				thebuffer[indx] += ds;
				/*
				 * Bump number of samples in this position
				 */
				bincnts[indx] += 1;
				aval = (alpha*ds) + (beta*aval);
			}
			else
			{
				skipped++;
			}
		}
		else
		{
			thebuffer[indx] += ds;
		    bincnts[indx] += 1;
		}
		
		/*
		 * Bump up our "segment" timer by the time-per-sample
		 */
		segt += sdt;
		
		/*
		 * One "segment" is one binwidth (pulsar-period / 256)
		 * If our segt after incrementing is >= binwidth, handle
		 *   bumping to next bin, and dealing with residual on
		 *   segment time.
		 */
		if (segt >= tpb)

		{
			/*
			 * Segt becomes whatever was left over from subtracting-out tpb
			 */
			segt = segt - tpb;
			
			/*
			 * Bump to the next bin
			 */
			indx ++;
			
			/*
			 * Handle wrap-around
			 */
			if (indx >= numbins)
			{
				indx = 0;
			}
		}
	}
	fprintf (stderr, "Processed %d seconds of data\n", (int)(totsamps/(long long int)srate));
	fprintf (stderr, "Skipped %d seconds of data\n", (int)(skipped/(long long int)srate));
	
	{
		int maxloc = 0;
		int start = 0;
		
		/*
		 * Dump the folded buffer
		 */
		minv = 9e9;
		maxv = 1e-9;
		for (i = 0; i < numbins; i++)
		{
			if ((thebuffer[i]/bincnts[i]) < minv)
			{
				minv = thebuffer[i]/bincnts[i];
			}
			if ((thebuffer[i]/bincnts[i]) > maxv)
			{
				maxv = (thebuffer[i]/bincnts[i]);
				maxloc = i;
			}
		}
		start = numbins/2;
		for (i = 0; i < numbins; i++)
		{
		    theotherbuffer[start] = thebuffer[maxloc];
		    start++;
		    maxloc++;
		    if (start >= numbins)
		    {
				start = 0;
			}
			if (maxloc >= numbins)
			{
				maxloc = 0;
			}
		}
	}
	
	for (i = 0; i < numbins; i++)
	{
		double lg;
		double linear;
		
		linear = theotherbuffer[i]/bincnts[i];
		lg = 10.0*log10(theotherbuffer[i]/bincnts[i]);
		fprintf (stdout, "%f %12.9f\n", (double)i*tpb, logmode ? lg : linear);
	}
	exit (0);
}
		
		
		
	
