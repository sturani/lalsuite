/*
*  Copyright (C) 2007 Gareth Jones
*
*  This program is free software; you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 2 of the License, or
*  (at your option) any later version.
*
*  This program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with with program; see the file COPYING. If not, write to the
*  Free Software Foundation, Inc., 59 Temple Place, Suite 330, Boston,
*  MA  02111-1307  USA
*/

/*-----------------------------------------------------------------------
 *
 * File Name: sned.c
 *
 * Author: Keppel, D. G., Jones, G. W.
 *
 * Revision: $Id$
 *
 *-----------------------------------------------------------------------
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <regex.h>
#include <time.h>
#include <math.h>

#include <FrameL.h>

#include <lalapps.h>
#include <series.h>
#include <processtable.h>
#include <lalappsfrutils.h>

#include <lal/LALConfig.h>
#include <lal/LALStdio.h>
#include <lal/LALStdlib.h>
#include <lal/LALError.h>
#include <lal/LALDatatypes.h>
#include <lal/AVFactories.h>
#include <lal/LALConstants.h>
#include <lal/FrameStream.h>
#include <lal/ResampleTimeSeries.h>
#include <lal/Calibration.h>
#include <lal/FrameCalibration.h>
#include <lal/Window.h>
#include <lal/TimeFreqFFT.h>
#include <lal/IIRFilter.h>
#include <lal/BandPassTimeSeries.h>
#include <lal/LIGOMetadataTables.h>
#include <lal/LIGOMetadataUtils.h>
#include <lal/LIGOLwXML.h>
#include <lal/LIGOLwXMLRead.h>
#include <lal/Date.h>
#include <lal/Units.h>
#include <lal/FindChirp.h>
#include <lal/FindChirpSP.h>
#include <lal/FindChirpTD.h>
#include <lal/FindChirpBCV.h>
#include <lal/FindChirpBCVSpin.h>
#include <lal/FindChirpChisq.h>
#include <lal/LALTrigScanCluster.h>
#include <lal/PrintFTSeries.h>
#include <lal/ReadFTSeries.h>
#include <lal/FrequencySeries.h>
#include <lal/GenerateInspiral.h>
#include <lal/TimeSeries.h>
#include <lal/VectorOps.h>

RCSID( "$Id$" );

#define CVS_ID_STRING "$Id$"
#define CVS_NAME_STRING "$Name$"
#define CVS_REVISION "$Revision$"
#define CVS_SOURCE "$Source$"
#define CVS_DATE "$Date$"
#define PROGRAM_NAME "sned"

#define ADD_PROCESS_PARAM( pptype, format, ppvalue ) \
  this_proc_param = this_proc_param->next = (ProcessParamsTable *) \
calloc( 1, sizeof(ProcessParamsTable) ); \
LALSnprintf( this_proc_param->program, LIGOMETA_PROGRAM_MAX, "%s", \
    PROGRAM_NAME ); \
LALSnprintf( this_proc_param->param, LIGOMETA_PARAM_MAX, "--%s", \
    long_options[option_index].name ); \
LALSnprintf( this_proc_param->type, LIGOMETA_TYPE_MAX, "%s", pptype ); \
LALSnprintf( this_proc_param->value, LIGOMETA_VALUE_MAX, format, ppvalue );

#define MAX_PATH 4096


#define USAGE \
  "lalapps_sned [options]\n"\
"\nDefaults are shown in brackets\n\n" \
"  --help                   display this message\n"\
"  --verbose                be verbose\n"\
"  --version                version info\n"\
"  --debug-level LEVEL      set the LAL debug level to LEVEL\n"\
"  --spinning-search        use the normalization for a spinning search\n"\
"                           instead of for a non-spinning search\n"\
"  --inject-overhead        inject signals from overhead detector\n"\
"  --write-chan             write out time series showing inspiral waveform\n"\
"  --inj-file    FILE       xml FILE contains injections\n"\
"  --coire-flag             use this if inj file is a coire file\n"\
"  --output-file FILE       FILE for output\n"\
"  --f-lower     FREQ       freq at which to begin integration\n"\
"  --ligo-only              only normalize the eff_dist columns for\n"\
"                           LIGO detectors\n"\
"\n"

static void destroyCoherentGW( CoherentGW *waveform );

static void destroyCoherentGW( CoherentGW *waveform )
{
  if ( waveform->h )
  {
    XLALDestroyREAL4VectorSequence( waveform->h->data );
    LALFree( waveform->a );
  }
  if ( waveform->a )
  {
    XLALDestroyREAL4VectorSequence( waveform->a->data );
    LALFree( waveform->a );
  }
  if ( waveform->phi )
  {
    XLALDestroyREAL8Vector( waveform->phi->data );
    LALFree( waveform->phi );
  }
  if ( waveform->f )
  {
    XLALDestroyREAL4Vector( waveform->f->data );
    LALFree( waveform->f );
  }
  if ( waveform->shift )
  {
    XLALDestroyREAL4Vector( waveform->shift->data );
    LALFree( waveform->shift );
  }

  return;
}

extern int vrbflg;           /* verbocity of lal function */
int writechan;               /* whether to write chan txt files */
int injoverhead;             /* perform inj overhead if this option is set */
int coireflg;                /* is input file coire (1) or inj (null) */
int nonSpinningSearch = 1;   /* normalize for a non-spinning search */
int ligoOnly = 0;            /* only normalize the LIGO eff_dist columns */

int main( int argc, char *argv[] )
{
  LALStatus                     status = blank_status;

  UINT4                         k;
  UINT4                         kLow;
  UINT4                         kHi;
  REAL4                         fSampling       = 16384.;
  REAL4                         fReSampling     = 4096.;
  REAL4                         fLow            = 70.;
  REAL4                         fLowInj         = 40.;
  INT4                          numPoints       = 1048576;
  INT4                          numRawPoints    = floor(
      numPoints * fSampling / fReSampling + 0.5 );
  REAL8                         deltaT          = 1./fSampling;
  REAL8                         deltaTReSample  = 1./fReSampling;
  REAL8                         deltaF          = fReSampling / numPoints;

  ResampleTSParams              resampleParams;

  REAL4                         statValue;
 
  /* vars required to make freq series */
  LIGOTimeGPS                   epoch = { 0, 0 };
  LIGOTimeGPS                   gpsStartTime = {0, 0}; 
  REAL8                         f0 = 0.;
  REAL8                         offset = 0.;
  INT8                          waveformStartTime = 0;

  /* files contain PSD info */
  CHAR                         *injectionFile = NULL;         
  CHAR                         *outputFile    = NULL;         

  COMPLEX8Vector               *unity = NULL;
  const LALUnit strainPerCount = {0,{0,0,0,0,0,1,-1},{0,0,0,0,0,0,0}};

  int                           numInjections = 0;
  int                           numTriggers = 0;

  /* template bank simulation variables */
  INT4                         injSimCount = 0;
  SimInspiralTable            *injectionHead  = NULL;
  SimInspiralTable            *thisInjection  = NULL;
  SimInspiralTable            *thisNonSpinningInjection = NULL;
  SnglInspiralTable           *snglHead       = NULL;
  SearchSummaryTable          *searchSummHead = NULL;
  /* SummValueTable              *summValueHead  = NULL; */

  /* raw input data storage */
  COMPLEX8FrequencySeries       *resp          = NULL;
  COMPLEX8FrequencySeries       *detTransDummy = NULL;
  REAL4TimeSeries               *chan          = NULL;
  REAL4TimeSeries               *chanDummy     = NULL;
  RealFFTPlan                   *pfwd          = NULL;
  COMPLEX8FrequencySeries       *fftData       = NULL;
  COMPLEX8FrequencySeries       *fftStandardData = NULL;
  REAL8                          thisSigmasq     = 0;
  REAL8                          spinningSigmasqVec[6] = {1, 1, 1, 1, 1, 1};
  REAL8                          thisStandardSigmasq = 0;
  REAL8                          standardSigmasqVec[6] = {1, 1, 1, 1, 1, 1};
  REAL8                          thisMixedSigmasq = 0;
  REAL8                          mixedSigmasqVec[6] = {1, 1, 1, 1, 1, 1};
  REAL8                          dynRange      = 1./(3.0e-23);

  /* needed for inj */
  CoherentGW                 waveform;
  CoherentGW                 nonSpinningWaveform;
  PPNParamStruc              ppnParams;
  DetectorResponse           detector;
  INT4                       ifos[6] = {1, 1, 1, 1, 1, 1};
  InterferometerNumber       ifoNumber = LAL_UNKNOWN_IFO;

  /* output data */
  LIGOLwXMLStream       xmlStream;
  LALLeapSecAccuracy    accuracy = LALLEAPSEC_LOOSE;
  MetadataTable         proctable;
  MetadataTable         outputTable;
  MetadataTable         procparams;
  CHAR                  fname[256];         
  CHAR                  comment[LIGOMETA_COMMENT_MAX];
  ProcessParamsTable   *this_proc_param = NULL;

  CHAR   chanfilename[FILENAME_MAX];

  /* set initial debug level */
  set_debug_level("1");

  /* create the process and process params tables */
  proctable.processTable = (ProcessTable *) calloc( 1, sizeof(ProcessTable) );
  LAL_CALL(LALGPSTimeNow(&status, &(proctable.processTable->start_time), 
                                                         &accuracy), &status);
  LAL_CALL( populate_process_table( &status, proctable.processTable, 
                PROGRAM_NAME, CVS_REVISION, CVS_SOURCE, CVS_DATE ), &status );
  this_proc_param = procparams.processParamsTable = (ProcessParamsTable *) 
                                      calloc( 1, sizeof(ProcessParamsTable) );
  memset( comment, 0, LIGOMETA_COMMENT_MAX * sizeof(CHAR) );

  /* look at input args, write process params where required */
  while ( 1 )
  {
    /* getopt arguments */
    static struct option long_options[] =
    {
      /* these options set a flag */
      /* these options do not set a flag */
      {"help",                    no_argument,       0,                 'h'},
      {"verbose",                 no_argument,       &vrbflg,            1 },
      {"version",                 no_argument,       0,                 'V'},
      {"inj-file",                required_argument, 0,                 'd'},
      {"comment",                 required_argument, 0,                 'e'},
      {"output-file",             required_argument, 0,                 'f'},
      {"coire-flag",              no_argument,       &coireflg,          1 },
      {"spinning-search",         no_argument,       &nonSpinningSearch, 0 },
      {"write-chan",              no_argument,       &writechan,         1 },
      {"inject-overhead",         no_argument,       &injoverhead,       1 },
      {"f-lower",                 required_argument, 0,                 'g'},
      {"ligo-only",               no_argument,       &ligoOnly,          1 },
      {"debug-level",             required_argument, 0,                 'z'}, 
      {0, 0, 0, 0}
    };
    int c;
  
    /*
     *
     * parse command line arguments
     *
     */

    /* getopt_long stores long option here */
    int option_index = 0;
    size_t optarg_len;

    c = getopt_long_only( argc, argv, "a:b:c:d:e:f:g:z:hV", long_options,
        &option_index );

    /* detect the end of the options */
    if ( c == - 1 )
    {
      break;
    }

    switch ( c )
    {
      case 0:
        /* if this option set a flag, do nothing else now */
        if ( long_options[option_index].flag != 0 )
        {
          break;
        }
        else
        {
          fprintf( stderr, "error parsing option %s with argument %s\n",
              long_options[option_index].name, optarg );
          exit( 1 );
        }
        break;

      case 'h':
        fprintf( stderr, USAGE );
        exit( 0 );
        break;

      case 'd':
        /* create storage for the injection file name */
        optarg_len = strlen( optarg ) + 1;
        injectionFile = (CHAR *) calloc( optarg_len, sizeof(CHAR));
        memcpy( injectionFile, optarg, optarg_len );
        ADD_PROCESS_PARAM( "string", "%s", optarg );
        break;

      case 'f':
        /* create storage for the output file name */
        optarg_len = strlen( optarg ) + 1;
        outputFile = (CHAR *) calloc( optarg_len, sizeof(CHAR));
        memcpy( outputFile, optarg, optarg_len );
        ADD_PROCESS_PARAM( "string", "%s", optarg );
        break;
    
      case 'g':
        fLow = (INT4) atof( optarg );
        if ( fLow > 40 )
        {
          fprintf( stderr, "invalid argument to --%s:\n"
              "f-lower must be < 40Hz (%e specified)\n",
              long_options[option_index].name, fLow );
          exit( 1 );
        }
        ADD_PROCESS_PARAM( "float", "%e", fLow );
        break;


     case 'e':
        if ( strlen( optarg ) > LIGOMETA_COMMENT_MAX - 1 )
        {
          fprintf( stderr, "invalid argument to --%s:\n"
              "comment must be less than %d characters\n",
              long_options[option_index].name, LIGOMETA_COMMENT_MAX );
          exit( 1 );
        }
        else
        {
          LALSnprintf( comment, LIGOMETA_COMMENT_MAX, "%s", optarg);
        }
        break;

      case 'V':
        /* print version information and exit */
        fprintf( stdout,
            "spin-normalize the effective distance for spinning injections\n"
            "Drew Keppel and Gareth Jones\n"
            "CVS Version: " CVS_ID_STRING "\n"
            "CVS Tag: " CVS_NAME_STRING "\n" );
        exit( 0 );
        break;

      case 'z':
        set_debug_level( optarg );
        ADD_PROCESS_PARAM( "string", "%s", optarg );
        break;
    
      default:
        fprintf( stderr, "unknown error while parsing options\n" );
        fprintf( stderr, USAGE );
        exit( 1 );
    }
  }  

  if ( optind < argc )
  {
    fprintf( stderr, "extraneous command line arguments:\n" );
    while ( optind < argc )
    {
      fprintf ( stderr, "%s\n", argv[optind++] );
    }
    exit( 1 );
  }

  /* check the input arguments */
  if ( injectionFile == NULL )
  {
    fprintf( stderr, "Must specify the --injection-file\n" );
    exit( 1 );
  }

  if ( outputFile == NULL )
  {
    fprintf( stderr, "Must specify the --output-file\n" );
    exit( 1 );
  }

  if ( vrbflg ){
    fprintf( stdout, "injection file is %s\n", injectionFile );
    fprintf( stdout, "output file is %s\n", outputFile );
  }

  /* read in injections from injection file */
  /* set endtime to 0 so that we read in all events */
  if ( vrbflg ) fprintf( stdout, "Reading sim_inspiral table of %s\n",
      injectionFile );
  LAL_CALL(numInjections = SimInspiralTableFromLIGOLw( &injectionHead,
      injectionFile, 0, 0), &status);
  if ( vrbflg ) fprintf( stdout,
      "Read %d injections from sim_inspiral table of %s\n", numInjections,
      injectionFile );

  if (coireflg)
  {
    if ( vrbflg ) fprintf( stdout, "Reading sngl_inspiral table of %s\n",
        injectionFile );
    LAL_CALL(numTriggers = LALSnglInspiralTableFromLIGOLw(&snglHead,
        injectionFile, 0, -1), &status);
    if ( vrbflg ) fprintf( stdout,
        "Read %d triggers from sngl_inspiral table of %s\n", numTriggers,
        injectionFile );
    if ( vrbflg )
    {
      fprintf( stdout, "Reading search_summary table of %s ...",
          injectionFile );
      fflush( stdout );
    }
    searchSummHead = XLALSearchSummaryTableFromLIGOLw (injectionFile);
    if ( vrbflg ) fprintf( stdout, " done\n");
  }

  if ( ligoOnly )
  {
    ifos[0] = 0;
    ifos[2] = 0;
    ifos[4] = 0;
    ifos[5] = 0;
  }

  /* make sure we start at head of linked list */
  thisInjection = injectionHead;

  LAL_CALL( LALCreateREAL4TimeSeries( &status, &chanDummy, "", epoch, f0,
      deltaT, lalADCCountUnit, numRawPoints ), &status );

  /*
   *
   * set up the response function
   *
   */
  LAL_CALL( LALCreateCOMPLEX8FrequencySeries( &status, &resp, chanDummy->name,
     chanDummy->epoch, f0, deltaF, strainPerCount, (numRawPoints / 2 + 1) ),
     &status );

  /* create vector that will contain detector.transfer info, since this 
   * is constant I calculate it once outside of all the loops and pass it 
   * in to detector.transfer when required 
   */
  LAL_CALL( LALCreateCOMPLEX8FrequencySeries( &status, &detTransDummy,
      chanDummy->name, chanDummy->epoch, f0, deltaF, strainPerCount,
      (numRawPoints / 2 + 1) ), &status );

  /* invert the response function to get the transfer function */
  unity = XLALCreateCOMPLEX8Vector( resp->data->length );
  for ( k = 0; k < unity->length; ++k )
  {
    unity->data[k].re = 1.0;
    unity->data[k].im = 0.0;
  }

  /* set response */
  for ( k = 0; k < resp->data->length; ++k )
  {
    resp->data->data[k].re = 1.0;
    resp->data->data[k].im = 0.0;
  }

  XLALCCVectorDivide( detTransDummy->data, unity, resp->data );
  XLALDestroyCOMPLEX8Vector( unity );

  /* setting fixed waveform injection parameters */
  memset( &ppnParams, 0, sizeof(PPNParamStruc) );
  ppnParams.deltaT   = deltaT;
  ppnParams.lengthIn = 0;
  ppnParams.ppn      = NULL;

  /* set up resampling parameters */   
  memset( &resampleParams, 0, sizeof(ResampleTSParams) );
  resampleParams.deltaT = deltaTReSample;
  resampleParams.filterType = LDASfirLP;

  /* loop over injections */
  injSimCount = 0;

  do
  {
    fprintf( stdout, "injection %d/%d\n", injSimCount+1, numInjections );

    /* reset waveform structure */
    memset( &waveform, 0, sizeof(CoherentGW) );
    memset( &nonSpinningWaveform, 0, sizeof(CoherentGW) );

    if (thisInjection->f_lower == 0)\
    {
      fprintf( stdout, "WARNING: f_lower in sim_inpiral = 0, ");
      fprintf( stdout, "changing this to %e\n ", fLowInj);
      thisInjection->f_lower = fLowInj;
    }

    /* create the waveform, amp, freq phase etc */
    LAL_CALL( LALGenerateInspiral(&status, &waveform, thisInjection,
        &ppnParams), &status );

    /* create the non-spinning waveform, amp, freq phase etc */
    thisNonSpinningInjection = thisInjection;
    strcpy(thisNonSpinningInjection->waveform, "TaylorT1threePointFivePN\0");
    LAL_CALL( LALGenerateInspiral(&status, &nonSpinningWaveform,
        thisNonSpinningInjection, &ppnParams), &status);
    if (vrbflg) fprintf( stdout, "ppnParams.tc %e\n ", ppnParams.tc);

    statValue = 0.;
  
    /* calc lower index for integration */
    kLow = ceil(fLow / deltaF);
    if ( vrbflg )
    {
      fprintf( stdout, "starting integration to find sigmasq at frequency %e ",
          fLow);
      fprintf( stdout, "at index %d \n", kLow);
    }
    /* calc upper index for integration */
    kHi = floor(fReSampling / (2. * deltaF));
    if ( vrbflg )
    {
      fprintf( stdout, "ending integration to find sigmasq at frequency %e ",
          fReSampling / 2.);
      fprintf( stdout, "at index %d \n", kHi);
    }

    /* loop over ifo */
    for ( ifoNumber = 0; ifoNumber < 6; ifoNumber++ )
    {
      if ( ifos[ifoNumber] )
      {
        /* allocate memory and copy the parameters describing the freq series */
        memset( &detector, 0, sizeof( DetectorResponse ) );
        detector.site = (LALDetector *) LALMalloc( sizeof(LALDetector) );

        if (injoverhead)
        { 
          if ( vrbflg ) fprintf( stdout,
              "WARNING: perform overhead injections\n");
          /* setting detector.site to NULL causes SimulateCoherentGW to
           * perform overhead injections */  
          detector.site = NULL; 
        }
        else
        {
          /* if not overhead, set detector.site using ifonumber */  
          XLALReturnDetector( detector.site, ifoNumber );
        } 

        if (vrbflg) fprintf(stdout,
            "generating chan to put waveform in\n" );

        LAL_CALL( LALCreateREAL4TimeSeries( &status, &chan, "", epoch, f0,
            deltaT, lalADCCountUnit, numRawPoints ), &status );

        /* reset chan structure */
        memset( chan->data->data, 0, chan->data->length * sizeof(REAL4) );

        /* get the gps start time of the signal to inject */
        LALGPStoINT8( &status, &waveformStartTime, 
            &(thisInjection->geocent_end_time) );
        waveformStartTime -= (INT8) ( 1000000000.0 * ppnParams.tc );

        offset = (chan->data->length / 2.0) * chan->deltaT;
        gpsStartTime.gpsSeconds =
            thisInjection->geocent_end_time.gpsSeconds - offset;
        gpsStartTime.gpsNanoSeconds =
            thisInjection->geocent_end_time.gpsNanoSeconds;
        chan->epoch = gpsStartTime;


        if (vrbflg) fprintf(stdout,
            "offset start time of injection by %f seconds \n", offset ); 
       
        /* is this okay? copying in detector transfer which so far only
         * contains response info  */
        detector.transfer = detTransDummy;

        XLALUnitInvert( &(detector.transfer->sampleUnits),
            &(resp->sampleUnits) );

        /* set the start times for injection */
        LALINT8toGPS( &status, &(waveform.a->epoch), &waveformStartTime );
        memcpy(&(waveform.f->epoch), &(waveform.a->epoch),
            sizeof(LIGOTimeGPS) );
        memcpy(&(waveform.phi->epoch), &(waveform.a->epoch),
            sizeof(LIGOTimeGPS) );
        memcpy(&(nonSpinningWaveform.a->epoch), &(waveform.a->epoch),
            sizeof(LIGOTimeGPS) );
        memcpy(&(nonSpinningWaveform.f->epoch), &(waveform.a->epoch),
            sizeof(LIGOTimeGPS) );
        memcpy(&(nonSpinningWaveform.phi->epoch), &(waveform.a->epoch),
            sizeof(LIGOTimeGPS) );

        /* perform the non-spinning injection */
        LAL_CALL( LALSimulateCoherentGW(&status, chan, &nonSpinningWaveform,
            &detector ), &status);
        LAL_CALL( LALResampleREAL4TimeSeries( &status, chan,
            &resampleParams ), &status );

        if (writechan)
        {
          /* write out channel data */
          if (vrbflg) fprintf(stdout, "writing channel data to file... \n" );
          switch ( ifoNumber )
          {
            case LAL_IFO_G1:
              LALSnprintf(chanfilename, FILENAME_MAX,
                  "nonspinning_G1_inj%d.dat", injSimCount+1);
              if (vrbflg) fprintf( stdout,
                  "writing G1 channel time series out to %s\n", chanfilename );
              LALSPrintTimeSeries(chan, chanfilename );
              break;
            case LAL_IFO_H1:
              LALSnprintf(chanfilename, FILENAME_MAX,
                  "nonspinning_H1_inj%d.dat", injSimCount+1);
              if (vrbflg) fprintf( stdout,
                  "writing H1 channel time series out to %s\n", chanfilename );
              LALSPrintTimeSeries(chan, chanfilename );
              break;
            case LAL_IFO_H2:
              LALSnprintf(chanfilename, FILENAME_MAX,
                  "nonspinning_H2_inj%d.dat", injSimCount+1);
              if (vrbflg) fprintf( stdout,
                  "writing H2 channel time series out to %s\n", chanfilename );
              LALSPrintTimeSeries(chan, chanfilename );
              break;
            case LAL_IFO_L1:
              LALSnprintf(chanfilename, FILENAME_MAX,
                  "nonspinning_L1_inj%d.dat", injSimCount+1);
              if (vrbflg) fprintf( stdout,
                  "writing L1 channel time series out to %s\n", chanfilename );
              LALSPrintTimeSeries(chan, chanfilename );
              break;
            case LAL_IFO_T1:
              LALSnprintf(chanfilename, FILENAME_MAX,
                  "nonspinning_T1_inj%d.dat", injSimCount+1);
              if (vrbflg) fprintf( stdout,
                  "writing T1 channel time series out to %s\n", chanfilename );
              LALSPrintTimeSeries(chan, chanfilename );
              break;
            case LAL_IFO_V1:
              LALSnprintf(chanfilename, FILENAME_MAX,
                  "nonspinning_V1_inj%d.dat", injSimCount+1);
              if (vrbflg) fprintf( stdout,
                  "writing V1 channel time series out to %s\n", chanfilename );
              LALSPrintTimeSeries(chan, chanfilename );
              break;
            default:
              fprintf( stderr,
                  "Error: ifoNumber %d does not correspond to a known IFO: \n",
                  ifoNumber );
              exit( 1 );
          }
        }

        LAL_CALL( LALCreateForwardRealFFTPlan( &status, &pfwd,
            chan->data->length, 0), &status);
        LAL_CALL( LALCreateCOMPLEX8FrequencySeries( &status, &fftStandardData,
            chan->name, chan->epoch, f0, deltaF, lalDimensionlessUnit,
            (numPoints / 2 + 1) ), &status );
        LAL_CALL( LALTimeFreqRealFFT( &status, fftStandardData, chan, pfwd ),
            &status);
        LAL_CALL( LALDestroyRealFFTPlan( &status, &pfwd ), &status);
        pfwd = NULL;

        /* reset chan structure */
        LAL_CALL( LALDestroyREAL4TimeSeries( &status, chan ), &status );
        LAL_CALL( LALCreateREAL4TimeSeries( &status, &chan, "", epoch, f0,
            deltaT, lalADCCountUnit, numRawPoints ), &status );

        /* get the gps start time of the signal to inject */
        LALGPStoINT8( &status, &waveformStartTime,
            &(thisInjection->geocent_end_time) );
        waveformStartTime -= (INT8) ( 1000000000.0 * ppnParams.tc );

        offset = (chan->data->length / 2.0) * chan->deltaT;
        gpsStartTime.gpsSeconds =
            thisInjection->geocent_end_time.gpsSeconds - offset;
        gpsStartTime.gpsNanoSeconds =
            thisInjection->geocent_end_time.gpsNanoSeconds;
        chan->epoch = gpsStartTime;

        /* perform the spinning injection */
        LAL_CALL( LALSimulateCoherentGW(&status, chan, &waveform, &detector ),
            &status);
        LAL_CALL( LALResampleREAL4TimeSeries( &status, chan,
            &resampleParams ), &status );

        if (writechan)
        {
          /* write out channel data */
          if (vrbflg) fprintf(stdout, "writing channel data to file... \n" );
          switch ( ifoNumber )
          {
            case LAL_IFO_G1:
              LALSnprintf( chanfilename, FILENAME_MAX, "spinning_G1_inj%d.dat",
                  injSimCount+1);
              if (vrbflg) fprintf( stdout,
                  "writing G1 channel time series out to %s\n", chanfilename );
              LALSPrintTimeSeries(chan, chanfilename );
              break;
            case LAL_IFO_H1:
              LALSnprintf( chanfilename, FILENAME_MAX, "spinning_H1_inj%d.dat",
                  injSimCount+1);
              if (vrbflg) fprintf( stdout,
                  "writing H1 channel time series out to %s\n", chanfilename );
              LALSPrintTimeSeries(chan, chanfilename );
              break;
            case LAL_IFO_H2:
              LALSnprintf( chanfilename, FILENAME_MAX, "spinning_H2_inj%d.dat",
                  injSimCount+1);
              if (vrbflg) fprintf( stdout,
                  "writing H2 channel time series out to %s\n", chanfilename );
              LALSPrintTimeSeries(chan, chanfilename );
              break;
            case LAL_IFO_L1:
              LALSnprintf( chanfilename, FILENAME_MAX, "spinning_L1_inj%d.dat",
                  injSimCount+1);
              if (vrbflg) fprintf( stdout,
                  "writing L1 channel time series out to %s\n", chanfilename );
              LALSPrintTimeSeries(chan, chanfilename );
              break;
            case LAL_IFO_T1:
              LALSnprintf( chanfilename, FILENAME_MAX, "spinning_T1_inj%d.dat",
                  injSimCount+1);
              if (vrbflg) fprintf( stdout,
                  "writing T1 channel time series out to %s\n", chanfilename );
              LALSPrintTimeSeries(chan, chanfilename );
              break;
            case LAL_IFO_V1:
              LALSnprintf( chanfilename, FILENAME_MAX, "spinning_V1_inj%d.dat",
                  injSimCount+1);
              if (vrbflg) fprintf( stdout,
                  "writing V1 channel time series out to %s\n", chanfilename );
              LALSPrintTimeSeries(chan, chanfilename );
              break;
            default:
              fprintf( stderr,
                  "Error: ifoNumber %d does not correspond to a known IFO: \n",
                  ifoNumber );
              exit( 1 );
          }
        }

        LAL_CALL( LALCreateForwardRealFFTPlan( &status, &pfwd,
            chan->data->length, 0), &status);
        LAL_CALL( LALCreateCOMPLEX8FrequencySeries( &status, &fftData,
            chan->name, chan->epoch, f0, deltaF, lalDimensionlessUnit,
            (numPoints / 2 + 1) ), &status );
        LAL_CALL( LALTimeFreqRealFFT( &status, fftData, chan, pfwd ), &status);
        LAL_CALL( LALDestroyRealFFTPlan( &status, &pfwd ), &status);
        pfwd = NULL;

        /* compute the Standard Sigmasq */
        thisStandardSigmasq = 0;

        /* avoid f=0 part of psd */
        {
          if (vrbflg)
          {
            switch ( ifoNumber )
            {
              case LAL_IFO_G1: fprintf( stdout, "using GEO PSD \n"); break;
              case LAL_IFO_H1:
                fprintf( stdout, "using LIGOI PSD with Hanford Location \n");
                break;
              case LAL_IFO_H2:
                fprintf( stdout, "using LIGOI PSD with Hanford Location \n");
                break;
              case LAL_IFO_L1:
                fprintf( stdout, "using LIGOI PSD with Livingston Location \n");
                break;
              case LAL_IFO_T1: fprintf( stdout, "using TAMA PSD \n"); break;
              case LAL_IFO_V1: fprintf( stdout, "using VIRGO PSD \n"); break;
              default:
                fprintf( stderr,
                    "Error: ifoNumber %d does not correspond to a known IFO: \n",
                    ifoNumber );
                exit( 1 );

            }
          }
          for ( k = kLow; k < kHi; k++ )
          {
            REAL8 freq;
            REAL8 sim_psd_value;
            freq = fftStandardData->deltaF * k;
            switch( ifoNumber )
            {
              case LAL_IFO_G1: LALGEOPsd( NULL, &sim_psd_value, freq ); break;
              case LAL_IFO_H1: LALLIGOIPsd( NULL, &sim_psd_value, freq ); break;
              case LAL_IFO_H2: LALLIGOIPsd( NULL, &sim_psd_value, freq ); break;
              case LAL_IFO_L1: LALLIGOIPsd( NULL, &sim_psd_value, freq ); break;
              case LAL_IFO_T1: LALTAMAPsd( NULL, &sim_psd_value, freq ); break;
              case LAL_IFO_V1: LALVIRGOPsd( NULL, &sim_psd_value, freq ); break;
              default:
                fprintf( stderr,
                    "Error: ifoNumber %d does not correspond to a known IFO: \n",
                    ifoNumber );
                exit( 1 );
            }

            thisStandardSigmasq +=
                ((fftStandardData->data->data[k].re * dynRange) *
                 (fftStandardData->data->data[k].re * dynRange)) /
                sim_psd_value;
            thisStandardSigmasq +=
                ((fftStandardData->data->data[k].im * dynRange) *
                 (fftStandardData->data->data[k].im * dynRange)) /
                sim_psd_value;
          }
        }

        thisStandardSigmasq *= 4*fftStandardData->deltaF;
        standardSigmasqVec[ifoNumber] = thisStandardSigmasq;
        if ( vrbflg )
        {
          fprintf( stdout, "thisStandardSigmasq %e\n", thisStandardSigmasq );
          fprintf( stdout, "standardSigmasqVec  %e\n",
              standardSigmasqVec[ifoNumber] );
          fflush( stdout );
        }

        /* compute the Mixed Sigmasq */
        thisMixedSigmasq = 0;

        /* avoid f=0 part of psd */
        {
          if (vrbflg)
          {
            switch ( ifoNumber )
            {
              case LAL_IFO_G1: fprintf( stdout, "using GEO PSD \n"); break;
              case LAL_IFO_H1:
                fprintf( stdout, "using LIGOI PSD with Hanford Location \n");
                break;
              case LAL_IFO_H2:
                fprintf( stdout, "using LIGOI PSD with Hanford Location \n");
                break;
              case LAL_IFO_L1:
                fprintf( stdout, "using LIGOI PSD with Livingston Location \n");
                break;
              case LAL_IFO_T1: fprintf( stdout, "using TAMA PSD \n"); break;
              case LAL_IFO_V1: fprintf( stdout, "using VIRGO PSD \n"); break;
              default:
                fprintf( stderr,
                    "Error: ifoNumber %d does not correspond to a known IFO: \n",
                    ifoNumber );
                exit( 1 );

            }
          }
          for ( k = kLow; k < kHi; k++ )
          {
            REAL8 numerator = 0.0;
            REAL8 freq;
            REAL8 sim_psd_value;
            freq = fftData->deltaF * k;
            switch( ifoNumber )
            { 
              case LAL_IFO_G1: LALGEOPsd( NULL, &sim_psd_value, freq ); break;
              case LAL_IFO_H1: LALLIGOIPsd( NULL, &sim_psd_value, freq ); break;
              case LAL_IFO_H2: LALLIGOIPsd( NULL, &sim_psd_value, freq ); break;
              case LAL_IFO_L1: LALLIGOIPsd( NULL, &sim_psd_value, freq ); break;
              case LAL_IFO_T1: LALTAMAPsd( NULL, &sim_psd_value, freq ); break;
              case LAL_IFO_V1: LALVIRGOPsd( NULL, &sim_psd_value, freq ); break;
              default:
                fprintf( stderr,
                    "Error: ifoNumber %d does not correspond to a known IFO: \n",
                    ifoNumber );
                exit( 1 );
            }

            numerator += pow((fftStandardData->data->data[k].re * dynRange) *
                             (fftData->data->data[k].re * dynRange) +
                             (fftStandardData->data->data[k].im * dynRange) *
                             (fftData->data->data[k].im * dynRange),2.0);
            numerator += pow((fftStandardData->data->data[k].im * dynRange) *
                             (fftData->data->data[k].re * dynRange) -
                             (fftStandardData->data->data[k].re * dynRange) *
                             (fftData->data->data[k].im * dynRange),2.0);

            thisMixedSigmasq += pow(numerator,0.5) / sim_psd_value;
          }
        }

        thisMixedSigmasq *= 4*fftData->deltaF;
        mixedSigmasqVec[ifoNumber] = thisMixedSigmasq;

        if ( vrbflg )
        {
          fprintf( stdout, "thisMixedSigmasq %e\n", thisMixedSigmasq );
          fprintf( stdout, "mixedSigmasqVec  %e\n",
              mixedSigmasqVec[ifoNumber] );
          fflush( stdout );
        }

        /* compute the Spinning sigmasq */
        thisSigmasq = 0;

        /* avoid f=0 part of psd */  
        {
          if (vrbflg)
          {
            switch ( ifoNumber )
            {
              case LAL_IFO_G1: fprintf( stdout, "using GEO PSD \n"); break;
              case LAL_IFO_H1:
                fprintf( stdout, "using LIGOI PSD with Hanford Location \n");
                break;
              case LAL_IFO_H2:
                fprintf( stdout, "using LIGOI PSD with Hanford Location \n");
                break;
              case LAL_IFO_L1:
                fprintf( stdout, "using LIGOI PSD with Livingston Location \n");
                break;
              case LAL_IFO_T1: fprintf( stdout, "using TAMA PSD \n"); break;
              case LAL_IFO_V1: fprintf( stdout, "using VIRGO PSD \n"); break;
              default:
                fprintf( stderr,
                    "Error: ifoNumber %d does not correspond to a known IFO: \n",
                    ifoNumber );
                exit( 1 );

            }
          }
          for ( k = kLow; k < kHi; k++ )
          {
            REAL8 freq;
            REAL8 sim_psd_value;
            freq = fftData->deltaF * k;
            switch( ifoNumber )
            { 
              case LAL_IFO_G1: LALGEOPsd( NULL, &sim_psd_value, freq ); break;
              case LAL_IFO_H1: LALLIGOIPsd( NULL, &sim_psd_value, freq ); break;
              case LAL_IFO_H2: LALLIGOIPsd( NULL, &sim_psd_value, freq ); break;
              case LAL_IFO_L1: LALLIGOIPsd( NULL, &sim_psd_value, freq ); break;
              case LAL_IFO_T1: LALTAMAPsd( NULL, &sim_psd_value, freq ); break;
              case LAL_IFO_V1: LALVIRGOPsd( NULL, &sim_psd_value, freq ); break;
              default:
                fprintf( stderr,
                    "Error: ifoNumber %d does not correspond to a known IFO: \n",
                    ifoNumber );
                exit( 1 );
           }

            thisSigmasq +=
                ((fftData->data->data[k].re * dynRange) * 
                 (fftData->data->data[k].re * dynRange)) /
                sim_psd_value;
            thisSigmasq +=
                ((fftData->data->data[k].im * dynRange) * 
                 (fftData->data->data[k].im * dynRange)) /
                sim_psd_value;
          }
        }

        thisSigmasq *= 4*fftData->deltaF;
        spinningSigmasqVec[ifoNumber] = thisSigmasq; 
        LAL_CALL( LALDestroyCOMPLEX8FrequencySeries( &status, fftData),
            &status );
        LAL_CALL( LALDestroyCOMPLEX8FrequencySeries( &status, fftStandardData),
            &status );

        if ( vrbflg )
        {
          fprintf( stdout, "thisSigmasq        %e\n", thisSigmasq );
          fprintf( stdout, "spinningSigmasqVec %e\n",
               spinningSigmasqVec[ifoNumber] );
          fflush( stdout );
        }

        /* free some memory */
        if (detector.transfer) detector.transfer = NULL;
        if ( detector.site )
        {
          LALFree( detector.site);
          detector.site = NULL;
        }
        LAL_CALL( LALDestroyREAL4TimeSeries( &status, chan ), &status );
      }
    }
    /* end loop over ifo */

    destroyCoherentGW( &waveform );
    destroyCoherentGW( &nonSpinningWaveform );

    /* normalize the eff_dist columns */
    if ( nonSpinningSearch )
    {
      thisInjection->eff_dist_g *=
          standardSigmasqVec[LAL_IFO_G1]/mixedSigmasqVec[LAL_IFO_G1];
      thisInjection->eff_dist_h *=
          standardSigmasqVec[LAL_IFO_H1]/mixedSigmasqVec[LAL_IFO_H1];
      thisInjection->eff_dist_l *=
          standardSigmasqVec[LAL_IFO_L1]/mixedSigmasqVec[LAL_IFO_L1];
      thisInjection->eff_dist_t *=
          standardSigmasqVec[LAL_IFO_T1]/mixedSigmasqVec[LAL_IFO_T1];
      thisInjection->eff_dist_v *=
          standardSigmasqVec[LAL_IFO_V1]/mixedSigmasqVec[LAL_IFO_V1];
    }
    else
    {
      thisInjection->eff_dist_g *=
          pow( standardSigmasqVec[LAL_IFO_G1]/spinningSigmasqVec[LAL_IFO_G1],
              0.5 );
      thisInjection->eff_dist_h *=
          pow( standardSigmasqVec[LAL_IFO_H1]/spinningSigmasqVec[LAL_IFO_H1],
              0.5 );
      thisInjection->eff_dist_l *=
          pow( standardSigmasqVec[LAL_IFO_L1]/spinningSigmasqVec[LAL_IFO_L1],
              0.5 );
      thisInjection->eff_dist_t *=
          pow( standardSigmasqVec[LAL_IFO_T1]/spinningSigmasqVec[LAL_IFO_T1],
              0.5 );
      thisInjection->eff_dist_v *=
          pow( standardSigmasqVec[LAL_IFO_V1]/spinningSigmasqVec[LAL_IFO_V1],
              0.5 );
    }

    /* increment the bank sim sim_inspiral table if necessary */
    if ( injectionHead )
    {
      thisInjection = thisInjection->next;
    }
  } while ( ++injSimCount < numInjections ); 
  /* end loop over injections */

  /* try opening, writing and closing an xml file */

  /* open the output xml file */
  memset( &xmlStream, 0, sizeof(LIGOLwXMLStream) );
  LALSnprintf( fname, sizeof(fname), outputFile );
  LAL_CALL( LALOpenLIGOLwXMLFile( &status, &xmlStream, fname ), &status );

  /* write out the process and process params tables */
  if ( vrbflg ) fprintf( stdout, "process... " );
  LAL_CALL( LALGPSTimeNow( &status, &(proctable.processTable->end_time ),
      &accuracy ), &status );
  LAL_CALL( LALBeginLIGOLwXMLTable( &status, &xmlStream, process_table ),
      &status );
  LAL_CALL( LALWriteLIGOLwXMLTable( &status, &xmlStream, proctable,
      process_table ), &status );
  LAL_CALL( LALEndLIGOLwXMLTable( &status, &xmlStream ), &status );
  free( proctable.processTable );
  /* Just being pedantic here ... */
  proctable.processTable = NULL;
 
  /* free the unused process param entry */
  this_proc_param = procparams.processParamsTable;
  procparams.processParamsTable = procparams.processParamsTable->next;
  free( this_proc_param );
  this_proc_param = NULL;

  /* write the process params table */
  if ( vrbflg ) fprintf( stdout, "process_params... " );
  LAL_CALL( LALBeginLIGOLwXMLTable( &status, &xmlStream,
      process_params_table ), &status );
  LAL_CALL( LALWriteLIGOLwXMLTable( &status, &xmlStream, procparams,
      process_params_table ), &status );
  LAL_CALL( LALEndLIGOLwXMLTable( &status, &xmlStream ), &status );

  /* write the search summary table */
  if ( coireflg )
  {
    if ( vrbflg ) fprintf( stdout, "search_summary... " );
    outputTable.searchSummaryTable = searchSummHead;
    LAL_CALL( LALBeginLIGOLwXMLTable( &status, &xmlStream,
      search_summary_table ), &status );
    LAL_CALL( LALWriteLIGOLwXMLTable( &status, &xmlStream, outputTable,
        search_summary_table ), &status );
    LAL_CALL( LALEndLIGOLwXMLTable( &status, &xmlStream ), &status );
  }

  /* write the sim inspiral table */
  if ( vrbflg ) fprintf( stdout, "sim_inspiral... " );
  outputTable.simInspiralTable = injectionHead;
  LAL_CALL( LALBeginLIGOLwXMLTable( &status, &xmlStream, sim_inspiral_table ),
      &status );
  LAL_CALL( LALWriteLIGOLwXMLTable( &status, &xmlStream, outputTable,
      sim_inspiral_table ), &status );
  LAL_CALL( LALEndLIGOLwXMLTable( &status, &xmlStream ), &status );

  /* write the sngl inspiral table */
  if ( coireflg )
  {
    if ( vrbflg ) fprintf( stdout, "sngl_inspiral... " );
    outputTable.snglInspiralTable = snglHead;
    LAL_CALL( LALBeginLIGOLwXMLTable( &status, &xmlStream,
        sngl_inspiral_table ), &status );
    LAL_CALL( LALWriteLIGOLwXMLTable( &status, &xmlStream, outputTable,
        sngl_inspiral_table ), &status );
    LAL_CALL( LALEndLIGOLwXMLTable( &status, &xmlStream ), &status );
  } 

  /* close the xml file */ 
  LAL_CALL( LALCloseLIGOLwXMLFile( &status, &xmlStream ), &status );

  free( injectionFile ); 
  injectionFile = NULL;

  /* free the process params */
  while( procparams.processParamsTable )
  {
    this_proc_param = procparams.processParamsTable;
    procparams.processParamsTable = this_proc_param->next;
    free( this_proc_param );
    this_proc_param = NULL;
  }

  /* free the sim inspiral tables */
  while ( injectionHead )
  {
    thisInjection = injectionHead;
    injectionHead = injectionHead->next;
    LALFree( thisInjection );
  }

  /* Freeing memory */
  LAL_CALL( LALDestroyREAL4TimeSeries( &status, chanDummy ), &status );
  LAL_CALL( LALDestroyCOMPLEX8FrequencySeries( &status, resp ), &status );
  LAL_CALL( LALDestroyCOMPLEX8FrequencySeries( &status, detTransDummy ),
      &status );


  /*check for memory leaks */
  LALCheckMemoryLeaks(); 

  /*print a success message to stdout for parsing by exitcode */
  fprintf( stdout, "\n%s: EXITCODE0\n", argv[0] );
  fflush( stdout );

  exit( 0 ); 
}
