# Copyright (C) 2014 Reed Essick
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 2 of the License, or (at your
# option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
# Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#
#

usage = "laldetchar-idq-calibration.py [--options]"
description = \
"""
Written to estimate mappings between classifier rank and FAP, Eff, Likelihood, etc. rapidly using historical performance data.
This should be run often to ensure an accurate calibration of the realtime processes, and such jobs will be scheduled and launched from within the realtime process.

Will also check how accurate the previous calibrations have been by computing segments from FAP timeseries.
We expect that segments generated by thresholding on the FAP time-series at FAP=0.XYZ should correspond to an amount of time DURATION=0.XYZ*LIVETIME
"""

#===================================================================================================

import os
import sys
import time

import numpy as np

from collections import defaultdict

import traceback
import ConfigParser
from optparse import OptionParser

from laldetchar.idq import idq
#from laldetchar.idq import reed as idq
from laldetchar.idq import event
from laldetchar.idq import calibration

from glue.ligolw import ligolw
from glue.ligolw import utils as ligolw_utils
from glue.ligolw import lsctables
from glue.ligolw import table

from laldetchar import git_version

__author__ = 'Reed Essick <reed.essick@ligo.org>'
__version__ = git_version.id
__date__ = git_version.date

#===================================================================================================

parser = OptionParser(version='Name: %%prog\n%s'%git_version.verbose_msg,
                          usage=usage,
                          description=description)

parser.add_option('-v', '--verbose', default=False, action='store_true')

parser.add_option('-c', '--config', default='idq.ini', type='string', help='configuration file')

parser.add_option('-k', '--lock-file', dest='lockfile', help='use custom lockfile', metavar='FILE', default=None )

parser.add_option('-l', '--log-file', default='idq_calibration.log', type='string', help='log file')

parser.add_option("", "--mode", default="dat", type="string", help="the mode for how we compute the calibration, either \"dat\" or \"npy\". If mode=\"dat\", we pick up datfiles and perform a counting experiment to compute the rank->fap mapping. If mode=\"npy\", we pick up rank-frames and directly measure the deadtime to compute the rank->fap mapping. This only affects the roc and uroc files produced. All KDE files and FAPthr checks are the same regardless of mode")

parser.add_option('-s', '--gpsstart', dest="gpsstart", default=-np.infty, type='float', help='gps start time')
parser.add_option('-e', '--gpsstop', dest="gpsstop", default=np.infty, type='float', help='gps end time')

parser.add_option('-b', '--lookback', default='0', type='string', help="Number of seconds to look back and get data for training. Default is zero.\
        Can be either positive integer or 'infinity'. In the latter case, the lookback will be incremented at every stride and all data after --gps-start will be used in every training.")

parser.add_option('-f','--force',default=False, action='store_true', help="forces *uroc cache file to be updated, even if we have no data. Use with caution.")

parser.add_option("", "--ignore-science-segments", default=False, action="store_true")

parser.add_option("", "--dont-cluster", default=False, action="store_true")

parser.add_option("", "--no-robot-cert", default=False, action="store_true")

parser.add_option('', '--FAPthr', default=[], action="append", type='float', help='check calibration at this FAP value. This argument can be supplied multiple times to check multiple values.')

opts, args = parser.parse_args()

if opts.lookback != "infinity":
    lookback = int(opts.lookback)

if opts.mode not in ["dat", "npy", "gwf"]:
    raise ValueError("--mode=%s not understood"%opts.mode)

#===================================================================================================
### setup logger to record processes
logger = idq.setup_logger('idq_logger', opts.log_file, sys.stdout, format='%(asctime)s %(message)s')

sys.stdout = idq.LogFile(logger)
sys.stderr = idq.LogFile(logger)

#===================================================================================================
### check lockfile
if opts.lockfile:
    lockfp = idq.dieiflocked( opts.lockfile )

#===================================================================================================
### read global configuration file

config = ConfigParser.SafeConfigParser()
config.read(opts.config)

#mainidqdir = config.get('general', 'idqdir') ### get the main directory where idq pipeline is going to be running.

ifo = config.get('general', 'ifo')

usertag = config.get('general', 'usertag')
if usertag:
    usertag = "_%s"%usertag

#========================
# which classifiers
#========================
### ensure we have a section for each classifier and fill out dictionary of options
classifiersD, mla, ovl = idq.config_to_classifiersD( config )

### get combiners information and add these to classifiersD
combinersD, referenced_classifiers = idq.config_to_combinersD( config )
for combiner, value in combinersD.items():
    classifiersD[combiner] = value

classifiers = sorted(classifiersD.keys())

### compute channel names stored in frames
channameD = dict( (name, {'rank':idq.channame(ifo, name, "%s_rank"%usertag),
                          'fap':idq.channame(ifo, name, "%s_fap"%usertag),
                          'fapUL':idq.channame(ifo, name, "%s_fapUL"%usertag)}) for name in classifiers )

#if mla:
#    ### reading parameters from config file needed for mla
#    auxmvc_coinc_window = config.getfloat('build_auxmvc_vectors','time-window')
#    auxmc_gw_signif_thr = config.getfloat('build_auxmvc_vectors','signif-threshold')
#    auxmvc_selected_channels = config.get('general','selected-channels')
#    auxmvc_unsafe_channels = config.get('general','unsafe-channels')

#========================
# realtime
#========================
realtimedir = config.get('general', 'realtimedir')

#========================
# calibration
#========================
calibrationdir = config.get('general', 'calibrationdir')

stride = config.getint('calibration', 'stride')
delay = config.getint('calibration', 'delay')

calibration_cache = dict( (classifier, idq.Cachefile(idq.cache(calibrationdir, classifier, tag='_calibration%s'%usertag))) for classifier in classifiers )
kde_cache = dict( (classifier, idq.Cachefile(idq.cache(calibrationdir, classifier, tag='_calibration-kde%s'%usertag))) for classifier in classifiers )

min_num_gch = config.getfloat('calibration', 'min_num_gch')
min_num_cln = config.getfloat('calibration', 'min_num_cln')

emaillist = config.get('warnings', 'calibration')
errorthr = config.getfloat('warnings', 'calibration_errorthr')

uroc_nsamples = config.getint('calibration','urank_nsamples')
urank = np.linspace(1, 0, uroc_nsamples) ### uniformly spaced ranks used to sample ROC curves -> uroc

cluster_key = config.get('calibration', 'cluster_key')
cluster_win = config.getfloat('calibration', 'cluster_win')

kde_nsamples = config.getint('calibration','kde_num_samples')
kde = np.linspace(0, 1, kde_nsamples ) ### uniformly spaced ranks used to sample kde

max_num_gch = config.getint('calibration', 'max_num_gch')
max_num_cln = config.getint('calibration', 'max_num_cln')

#========================
# data discovery
#========================
if not opts.ignore_science_segments:
    ### load settings for accessing dmt segment files
#    dmt_segments_location = config.get('get_science_segments', 'xmlurl')
    dq_name = config.get('get_science_segments', 'include')
#    dq_name = config.get('get_science_segments', 'include').split(':')[1]
    segdb_url = config.get('get_science_segments', 'segdb')

#==================================================
### set up ROBOT certificates
### IF ligolw_segement_query FAILS, THIS IS A LIKELY CAUSE
if opts.no_robot_cert:
    logger.warning("WARNING: running without a robot certificate. Your personal certificate may expire and this job may fail")
else:
    ### unset ligo-proxy just in case
    if os.environ.has_key("X509_USER_PROXY"):
        del os.environ['X509_USER_PROXY']

    ### get cert and key from ini file
    robot_cert = config.get('ldg_certificate', 'robot_certificate')
    robot_key = config.get('ldg_certificate', 'robot_key')

    ### set cert and key
    os.environ['X509_USER_CERT'] = robot_cert
    os.environ['X509_USER_KEY'] = robot_key

#==================================================
### current time and boundaries

t = int(idq.nowgps())

gpsstop = opts.gpsstop
if not gpsstop: ### stop time of this analysis
    logger.info('computing gpsstop from current time')
    gpsstop = t ### We do not require boundaries to be integer multiples of stride

gpsstart = opts.gpsstart
if not gpsstart:
    logger.info('computing gpsstart from gpsstop')
    gpsstart = gpsstop - stride

#===================================================================================================
#
# LOOP
#
#===================================================================================================
logger.info('Begin: calibration')

### wait until all jobs are finished
wait = gpsstart + stride + delay - t
if wait > 0:
    logger.info('----------------------------------------------------')
    logger.info('waiting %.1f seconds to reach gpsstop+delay=%d' % (wait, delay))
    time.sleep(wait)

global_start = gpsstart

### iterate over all ranges
while gpsstart < gpsstop:

    logger.info('----------------------------------------------------')

    wait = gpsstart + stride + delay - idq.nowgps()
    if wait > 0:
        logger.info('waiting %.1f seconds to reach gpsstart+stride+delay=%d' %(wait, gpsstart+stride+delay))
        time.sleep(wait)

    logger.info('Begin: stride [%d, %d]'%(gpsstart, gpsstart+stride))

    ### directory into which we write data
    output_dir = "%s/%d_%d/"%(calibrationdir, gpsstart, gpsstart + stride)
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    if opts.lookback=="infinity":
        lookback = gpsstart - global_start

    #===============================================================================================
    # science segments
    # we query the segdb right now, although that latency may be an issue...
    #===============================================================================================
    if opts.ignore_science_segments:
        logger.info('analyzing data regardless of science segements')
        scisegs = [[gpsstart-lookback, gpsstart+stride]] ### set segs to be this stride range
        coveredsegs = [[gpsstart-lookback, gpsstart+stride]] ### set segs to be this stride range

    else:
        logger.info('Begin: querrying science segments')

        try:
            ### this returns a string
            seg_xml_file = idq.segment_query(config, gpsstart - lookback , gpsstart + stride, url=segdb_url)

            ### write seg_xml_file to disk
            lsctables.use_in(ligolw.LIGOLWContentHandler)
            xmldoc = ligolw_utils.load_fileobj(seg_xml_file, contenthandler=ligolw.LIGOLWContentHandler)[0]

            ### science segments xml filename
            seg_file = idq.segxml(output_dir, "_%s"%dq_name, gpsstart - lookback , lookback+stride)

            logger.info('writing science segments to file : '+seg_file)
            ligolw_utils.write_filename(xmldoc, seg_file, gz=seg_file.endswith(".gz"))

            (scisegs, coveredseg) = idq.extract_dq_segments(seg_file, dq_name) ### read in segments from xml file

        except Exception as e:
            traceback.print_exc()
            logger.info('ERROR: segment generation failed. Skipping this calibration period.')

            if opts.force: ### we are require successful training or else we want errors
                logger.info(traceback.print_exc())
                raise e
            else: ### we don't care if any particular training job fails
                gpsstart += stride
                continue

    logger.info('finding idq segments')
    idqsegs = idq.get_idq_segments(realtimedir, gpsstart-lookback, gpsstart+stride, suffix='.dat')

    logger.info('taking intersection between science segments and idq segments')
    idqsegs = event.andsegments( [scisegs, idqsegs] )

    idqsegs_livetime = event.livetime( idqsegs )

    ### write segment file
    if opts.ignore_science_segments:
        idqseg_path = idq.idqsegascii(output_dir, '', gpsstart-lookback, lookback+stride)
    else:
        idqseg_path = idq.idqsegascii(output_dir, '_%s'%dq_name, gpsstart - lookback, lookback+stride)
    f = open(idqseg_path, 'w')
    for seg in idqsegs:
        print >> f, seg[0], seg[1]
    f.close()

    #===============================================================================================
    # update mappings via uroc files
    #===============================================================================================

    ### find all *dat files, bin them according to classifier
    ### needed for opts.mode=="dat" and KDE estimates
    logger.info('finding all *dat files')
    datsD = defaultdict( list )
    for dat in idq.get_all_files_in_range(realtimedir, gpsstart-lookback, gpsstart+stride, pad=0, suffix='.dat' ):
        datsD[idq.extract_dat_name( dat )].append( dat )

    ### throw away any un-needed files
    for key in datsD.keys():
        if key not in classifiers:
            datsD.pop(key) 
        else: ### throw out files that don't contain any science time
            datsD[key] = [ dat for dat in datsD[key] if event.livetime(event.andsegments([idqsegs, [idq.extract_start_stop(dat, suffix='.dat')]])) ]

    if opts.mode == "dat": ### we just looked these up
        pass

    elif opts.mode=="npy": ### need rank files
        ### find all *rank*npy.gz files, bin them according to classifier
        logger.info('  finding all *rank*.npy.gz files')
        ranksD = defaultdict( list )
        for rank in [rank for rank in  idq.get_all_files_in_range(realtimedir, gpsstart-lookback, gpsstart+stride, pad=0, suffix='.npy.gz') if "rank" in rank]:
            ranksD[idq.extract_fap_name( rank )].append( rank ) ### should just work...

        ### throw away files we will never need
        for key in ranksD.keys():
            if key not in classifiers: ### throw away unwanted files
                ranksD.pop(key)
            else: ### keep only files that overlap with scisegs
                ranksD[key] = [ rank for rank in ranksD[key] if event.livetime(event.andsegments([idqsegs, [idq.extract_start_stop(rank, suffix='.npy.gz')]])) ]

    elif opts.mode=="gwf": ### need rank frames
        ### find all *rank*npy.gz files, bin them according to classifier
        logger.info('  finding all *rank*.gwf files')
        ranksD = defaultdict( list )
        for rank in [rank for rank in  idq.get_all_files_in_range(realtimedir, gpsstart-lookback, gpsstart+stride, pad=0, suffix='.gwf') if "rank" in rank]:
            ranksD[idq.extract_fap_name( rank )].append( rank ) ### should just work...

        ### throw away files we will never need
        for key in ranksD.keys():
            if key not in classifiers: ### throw away unwanted files
                ranksD.pop(key)
            else: ### keep only files that overlap with scisegs
                ranksD[key] = [ rank for rank in ranksD[key] if event.livetime(event.andsegments([idqsegs, [idq.extract_start_stop(rank, suffix='.gwf')]])) ]
    else:
        raise ValueError("mode=%s not understood"%opts.mode)

    #====================
    # update uroc for each classifier
    #====================
    urocs = {} ### stores uroc files for kde estimation
    for classifier in classifiers:
        ### write list of dats to cache file
        cache = idq.cache(output_dir, classifier, "_datcache%s"%usertag)
        logger.info('writing list of dat files to %s'%cache)
        f = open(cache, 'w')
        for dat in datsD[classifier]:
            print >>f, dat
        f.close()

        logger.info('  computing new calibration for %s'%classifier)

        ### extract data from dat files
        output = idq.slim_load_datfiles(datsD[classifier], skip_lines=0, columns='GPS i rank'.split()+[cluster_key])

        ### filter times by scisegs -> keep only the ones within scisegs
        output = idq.filter_datfile_output( output, idqsegs )

        ### cluster
        if not opts.dont_cluster:
            output = idq.cluster_datfile_output( output, cluster_key=cluster_key, cluster_win=cluster_win)

        ### downselect to only keep the most recent max_num_gch and max_num_cln
        these_columns, glitches, cleans = idq.separate_output( output )
        glitches.sort(key=lambda l: l[these_columns['GPS']])
        cleans.sort(key=lambda l: l[these_columns['GPS']])
        if len(glitches) > max_num_gch:
            logger.info('  downselecting to the %d most recent glitches'%max_num_gch)
            glitches = glitches[-max_num_gch:]
        if len(cleans) > max_num_cln:
            logger.info('  downselecting to the %d most recent cleans'%max_num_cln)
            cleans = cleans[-max_num_cln:]
        output = idq.combine_separated_output( these_columns, [glitches, cleans] )

        ### define weights over time
        output['weight'] = calibration.weights( output['GPS'], weight_type="uniform" )

        if not opts.dont_cluster:
            cluster_dat = idq.dat(output_dir, classifier, ifo, "clustered", usertag, gpsstart-lookback, lookback+stride) ### write clustered dat file
            logger.info('  writing %s'%cluster_dat)
            idq.output_to_datfile( output, cluster_dat )
        else:
            cluster_dat = idq.dat(output_dir, classifier, ifo, "unclustered", usertag, gpsstart-lookback, lookback+stride)
            logger.info('  writing %s'%cluster_dat)
            idq.output_to_datfile( output, cluster_dat )

        ### compute rcg from output
        r, c, g = idq.dat_to_rcg( output )

        logger.info('    N_gch = %d , N_cln = %d'%(g[-1], c[-1]))


        ### dump into roc file
        roc = idq.roc(output_dir, classifier, ifo, usertag, gpsstart-lookback, lookback+stride)
        logger.info('  writting %s'%roc)
        idq.rcg_to_file(roc, r, c, g)

        ### upsample to roc
        r, c, g = idq.resample_rcg(urank, r, c, g)
        urocs[classifier] = (r, c, g)

        if opts.mode == "dat": 
            ### dump uroc to file
            uroc = idq.uroc(output_dir, classifier, ifo, usertag, gpsstart-lookback, lookback+stride)
            logger.info('  writing %s'%uroc)
            idq.rcg_to_file(uroc, r, c, g)

            if opts.force or ((c[-1] >= min_num_cln) and (g[-1] >= min_num_gch)):
                ### update cache file
                logger.info('  adding %s to %s'%(uroc, calibration_cache[classifier].name) )
                calibration_cache[classifier].append( uroc )

            else:
                logger.warning('WARNING: not enough samples to trust calibration. skipping calibration update for %s'%classifier)

        elif (opts.mode == "npy") or (opts.mode == "gwf"):
            ### write list of dats to cache file
            cache = idq.cache(output_dir, classifier, "_rankcache%s"%usertag)
            logger.info('writing list of rank files to %s'%cache)
            f = open(cache, 'w')
            for rank in ranksD[classifier]:
                print >>f, rank
            f.close()

            logger.info('  analyzing rank timeseries to obtain mapping from rank->fap')

            ### load in timeseries
            if opts.mode == "npy":
                _times, timeseries = idq.combine_ts(ranksD[classifier], n=1)
            else: ### opts.mode=="gwf"
                _times, timeseries = idq.combine_gwf(ranksD[classifier], [channameD[classifier]['rank']])

            times = []
            ranks = []
            for t, ts in zip(_times, timeseries):
                _t, _ts = idq.timeseries_in_segments(t, ts, idqsegs)
                if len(_ts):
                    times.append( _t )
                    ranks.append( _ts )

            ### need to compute deadsecs for every rank in r -> function call (probably within calibration module)!
            crank = []
            for _r in r:

                dsec = 0
                for t, ts in zip(times, ranks):
                    dt = t[1]-t[0] ### get time spacing.
                    dsec += calibration.timeseries_to_livetime(dt, ts, _r)[0] # we don't care about segments, so just get livetime and smallest stated value
                crank.append( dsec )
 
            ### dump uroc file
            uroc = idq.uroc(output_dir, classifier, ifo, usertag, gpsstart-lookback, lookback+stride)
            logger.info('  writing %s'%uroc)
            idq.rcg_to_file(uroc, r, crank, g) ### use the amount of time identified via timeseries

            logger.warning('  WARNING: interpretation of this for Binomial upper limits is more complicated... but that happens within laldetchar-idq-realtime. probably need to set an option in idq.ini that controls how we compute this mapping. reference this within laldetchar-idq-realtime when computing pt. estimates and upper limits. ')

            if opts.force or ((c[-1] >= min_num_cln) and (g[-1] >= min_num_gch)):
                ### update cache file
                logger.info('  adding %s to %s'%(uroc, calibration_cache[classifier].name) )
                calibration_cache[classifier].append( uroc )

            else:
                logger.warning('WARNING: not enough samples to trust calibration. skipping calibration update for %s'%classifier)

        else:
            raise ValueError("mode=%s not understood"%opts.mode)

    #===============================================================================================
    # compute KDE estimates
    #===============================================================================================

    for classifier in classifiers:
        logger.info('computing KDE pdfs for %s'%classifier)

        r, c, g = urocs[classifier] 
        logger.info('  compute number of samples at each rank')
        dc, dg = idq.rcg_to_diff( c, g ) ### get the numbers at each rank

        logger.info('  computing KDE for cleans')
        kde_cln = idq.kde_pwg( kde, r, dc ) ### compute kde estimate
        logger.info('  computing KDE for glitches')
        kde_gch = idq.kde_pwg( kde, r, dg )

        ### write kde points to file
        kde_cln_name = idq.kdename(output_dir, classifier, ifo, "_cln%s"%usertag, gpsstart-lookback, lookback+stride)
        logger.info('  writing %s'%kde_cln_name)
        np.save(event.gzopen(kde_cln_name, "w"), (kde, kde_cln))

        kde_gch_name = idq.kdename(output_dir, classifier, ifo, "_gch%s"%usertag, gpsstart-lookback, lookback+stride)
        logger.info('  writing %s'%kde_gch_name)
        np.save(event.gzopen(kde_gch_name, "w"), (kde, kde_gch))

        ### update cache files
        if opts.force or ((c[-1] > min_num_cln) and (g[-1] >= min_num_gch)): 
            logger.info('  adding %s to %s'%(kde_cln_name, kde_cache[classifier].name))
            kde_cache[classifier].append( kde_cln_name )
            logger.info('  adding %s to %s'%(kde_gch_name, kde_cache[classifier].name))
            kde_cache[classifier].append( kde_gch_name )
        else:
            logger.warning('WARNING: not enough samples to trust calibration. skipping kde update for %s'%classifier)

    #===============================================================================================
    # check historical calibration, send alerts
    #===============================================================================================
    if opts.FAPthr: ### only if we have something to do
        logger.info('checking historical calibration for accuracy')

        ### find all *fap*npy.gz files, bin them according to classifier
#        logger.info('  finding all *fap*.npy.gz files')
        logger.info('  finding all *fap*.gwf files')
        fapsD = defaultdict( list )
#        for fap in [fap for fap in  idq.get_all_files_in_range(realtimedir, gpsstart-lookback, gpsstart+stride, pad=0, suffix='.npy.gz') if "fap" in fap]:
        for fap in [fap for fap in  idq.get_all_files_in_range(realtimedir, gpsstart-lookback, gpsstart+stride, pad=0, suffix='.gwf') if "fap" in fap]:
            fapsD[idq.extract_fap_name( fap )].append( fap )

        ### throw away files we will never need
        for key in fapsD.keys():
            if key not in classifiers: ### throw away unwanted files
                fapsD.pop(key)
            else: ### keep only files that overlap with scisegs
#                fapsD[key] = [ fap for fap in fapsD[key] if event.livetime(event.andsegments([idqsegs, [idq.extract_start_stop(fap, suffix='.npy.gz')]])) ]
                fapsD[key] = [ fap for fap in fapsD[key] if event.livetime(event.andsegments([idqsegs, [idq.extract_start_stop(fap, suffix='.gwf')]])) ]

        ### iterate through classifiers
        alerts = {} ### files that we should be alerted about
        for classifier in classifiers:
            logger.info('  checking calibration for %s'%classifier)

            cache = idq.cache(output_dir, classifier, "_fapcache%s"%usertag)
            logger.info('    writing list of fap files to %s'%cache)
            f = open(cache, 'w')
            for fap in fapsD[classifier]:
                print >>f, fap
            f.close()

            logger.info('    analyzing timeseries')
            if opts.mode=="dat": ### we have upper limits
#                _times, _timeseries = idq.combine_ts(fapsD[classifier], n=2) ### read in time-series
                _times, timeseries = idq.combine_gwf(fapsD[classifier], channels=[channameD[classifier]['fap'], channameD[classifier]['fapUL']]) ### read in time-series
 
                times = []
                faps = []
                fapsUL = []
                dt = 0.0
                for t, ts in zip(_times, timeseries):
                    if not dt:
                        dt = t[1]-t[0]
                    _t, _ts = idq.timeseries_in_segments(t, ts, idqsegs)
                    if len(_ts):
                        times.append( _t )
                        faps.append( _ts[0] )
                        fapsUL.append( _ts[1] )

                logger.info('    checking point estimate calibration')
                ### check point estimate calibration
#                _, deadtimes, statedFAPs, errs = calibration.check_calibration(idqsegs, times, faps, opts.FAPthr) 
                deadtimes, statedFAPs, errs = calibration.check_calibration_FAST(idqsegs_livetime, faps, opts.FAPthr, dt=dt) 

                logger.info('    checking upper limit calibration')
                ### check UL estimate calibration
#                _, deadtimesUL, statedFAPsUL, errsUL = calibration.check_calibration(idqsegs, times, fapsUL, opts.FAPthr)
                deadtimesUL, statedFAPsUL, errsUL = calibration.check_calibration_FAST(idqsegs_livetime, fapsUL, opts.FAPthr, dt=dt)

            elif (opts.mode=="npy") or (opts.mode=="gwf"): ### no upper limits currently...
#                _times, timeseries = idq.combine_ts(fapsD[classifier], n=1) ### read in time-series
                _times, timeseries = idq.combine_gwf(fapsD[classifier], [channameD[classifier]['fap']]) ### read in time-series

                times = []
                faps = []
                dt = 0.0
                for t, ts in zip(_times, timeseries):
                    if not dt:
                        dt = t[1]-t[0]
                    _t, _ts = idq.timeseries_in_segments(t, ts, idqsegs)
                    if len(_ts):
                        times.append( _t )
                        faps.append( _ts[0] )

                logger.info('    checking point estimate calibration')
                ### check point estimate calibration
#                _, deadtimes, statedFAPs, errs = calibration.check_calibration(idqsegs, times, faps, opts.FAPthr) 
                deadtimes, statedFAPs, errs = calibration.check_calibration_FAST(idqsegs_livetime, faps, opts.FAPthr, dt=dt)

                deadtimesUL = statedFAPsUL = errsUL = [np.nan for FAPthr in opts.FAPthr] ### place-holder
                
            else:
                raise ValueError("mode=%s not understood"%opts.mode)

            calib_check = idq.calib_check(output_dir, classifier, ifo, usertag, gpsstart-lookback, lookback+stride)
            logger.info('    writing %s'%calib_check)            

            file_obj = open(calib_check, "w")
            print >> file_obj, "livetime = %.3f"%idqsegs_livetime
            for FAPthr, deadtime, statedFAP, err, deadtimeUL, statedFAPUL, errUL in zip(opts.FAPthr, deadtimes, statedFAPs, errs, deadtimesUL, statedFAPsUL, errsUL):
                    print >> file_obj, calibration.report_str%(FAPthr, statedFAP, deadtime, err, statedFAPUL, deadtimeUL, errUL )
            file_obj.close()

            if np.any(np.abs(errs) > errorthr) or np.any(np.abs(errsUL) > errorthr):
                alerts[classifier] = calib_check

        if alerts: ### there are some interesting files
            alerts_keys = sorted(alerts.keys())
            alerts_keys_str = " ".join(alerts_keys)
            logger.warning('WARNING: found suspicous historical calibrations for : %s'%alerts_keys_str )
            if emaillist:
                email_cmd = "echo \"calibration check summary files are attached for: %s\" | mailx -s \"%s idq%s calibration warning in laldetchar-idq-calibration\" %s \"%s\""%(alerts_keys_str, ifo, usertag, " ".join("-a \"%s\""%alerts[key] for key in alerts_keys), emaillist)
                logger.warning("  %s"%email_cmd)
                exit_code = os.system( email_cmd )
                if exit_code:
                    logger.warning("WARNING: failed to send email!")

    """
    check ROC curves, channel statistics, etc. send alerts if something changes?
    """

    logger.info('Done: stride [%d, %d]'%(gpsstart, gpsstart+stride))

    gpsstart += stride

#===================================================================================================
if opts.lockfile:
    idq.release(lockfp) ### unlock lockfile
    os.remove( opts.lockfile )
