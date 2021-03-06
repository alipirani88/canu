
/******************************************************************************
 *
 *  This file is part of canu, a software program that assembles whole-genome
 *  sequencing reads into contigs.
 *
 *  This software is based on:
 *    'Celera Assembler' (http://wgs-assembler.sourceforge.net)
 *    the 'kmer package' (http://kmer.sourceforge.net)
 *  both originally distributed by Applera Corporation under the GNU General
 *  Public License, version 2.
 *
 *  Canu branched from Celera Assembler at its revision 4587.
 *  Canu branched from the kmer project at its revision 1994.
 *
 *  This file is derived from:
 *
 *    src/AS_BAT/bogart.C
 *
 *  Modifications by:
 *
 *    Brian P. Walenz from 2010-NOV-23 to 2014-JAN-29
 *      are Copyright 2010-2014 J. Craig Venter Institute, and
 *      are subject to the GNU General Public License version 2
 *
 *    Brian P. Walenz from 2014-OCT-21 to 2015-AUG-07
 *      are Copyright 2014-2015 Battelle National Biodefense Institute, and
 *      are subject to the BSD 3-Clause License
 *
 *    Brian P. Walenz beginning on 2015-DEC-07
 *      are a 'United States Government Work', and
 *      are released in the public domain
 *
 *    Sergey Koren beginning on 2016-MAR-11
 *      are a 'United States Government Work', and
 *      are released in the public domain
 *
 *  File 'README.licenses' in the root directory of this distribution contains
 *  full conditions and disclaimers for each license.
 */

#include "AS_BAT_ReadInfo.H"
#include "AS_BAT_OverlapCache.H"
#include "AS_BAT_BestOverlapGraph.H"
#include "AS_BAT_ChunkGraph.H"
#include "AS_BAT_AssemblyGraph.H"

#include "AS_BAT_Logging.H"

#include "AS_BAT_Unitig.H"

#include "AS_BAT_PopulateUnitig.H"
#include "AS_BAT_Instrumentation.H"
#include "AS_BAT_PlaceContains.H"

#include "AS_BAT_MergeOrphans.H"
#include "AS_BAT_MarkRepeatReads.H"

#include "AS_BAT_SplitDiscontinuous.H"

#include "AS_BAT_DropDeadEnds.H"

#include "AS_BAT_PromoteToSingleton.H"

#include "AS_BAT_CreateUnitigs.H"

#include "AS_BAT_SetParentAndHang.H"
#include "AS_BAT_Outputs.H"

#include "AS_BAT_TigGraph.H"


ReadInfo         *RI  = 0L;
OverlapCache     *OC  = 0L;
BestOverlapGraph *OG  = 0L;
ChunkGraph       *CG  = 0L;

int
main (int argc, char * argv []) {
  char      *gkpStorePath            = NULL;
  char      *ovlStorePath            = NULL;

  double    erateGraph               = 0.075;
  double    erateMax                 = 0.100;

  bool      filterSuspicious         = true;
  bool      filterHighError          = true;
  bool      filterLopsided           = true;
  bool      filterSpur               = true;
  bool      filterDeadEnds           = true;

  uint64    genomeSize               = 0;

  uint32    fewReadsNumber           = 2;      //  Parameters for labeling of unassembled; also set in pipelines/canu/Defaults.pm
  uint32    tooShortLength           = 0;
  double    spanFraction             = 1.0;
  double    lowcovFraction           = 0.5;
  uint32    lowcovDepth              = 3;

  double    deviationGraph           = 6.0;
  double    deviationBubble          = 6.0;
  double    deviationRepeat          = 3.0;

  uint32    confusedAbsolute         = 5000;
  double    confusedPercent          = 500.0;

  int32     numThreads               = 0;

  uint64    ovlCacheMemory           = UINT64_MAX;

  bool      doSave                   = false;

  char     *prefix                   = NULL;

  uint32    minReadLen               = 0;
  uint32    minOverlapLen            = 500;
  uint32    minIntersectLen          = 500;
  uint32    maxPlacements            = 2;

  argc = AS_configure(argc, argv);

  vector<char *>  err;
  int             arg = 1;
  while (arg < argc) {
    if        (strcmp(argv[arg], "-o") == 0) {
      prefix = argv[++arg];

    } else if (strcmp(argv[arg], "-G") == 0) {
      gkpStorePath = argv[++arg];

    } else if (strcmp(argv[arg], "-O") == 0) {
      ovlStorePath = argv[++arg];

    } else if (strcmp(argv[arg], "-gs") == 0) {
      genomeSize = strtoull(argv[++arg], NULL, 10);

    } else if (strcmp(argv[arg], "-unassembled") == 0) {
      uint32  invalid = 0;

      if ((arg + 1 < argc) && (argv[arg + 1][0] != '-'))
        fewReadsNumber  = atoi(argv[++arg]);
      else
        invalid++;

      if ((arg + 1 < argc) && (argv[arg + 1][0] != '-'))
        tooShortLength  = atoi(argv[++arg]);
      else
        invalid++;

      if ((arg + 1 < argc) && (argv[arg + 1][0] != '-'))
        spanFraction    = atof(argv[++arg]);
      else
        invalid++;

      if ((arg + 1 < argc) && (argv[arg + 1][0] != '-'))
        lowcovFraction  = atof(argv[++arg]);
      else
        invalid++;

      if ((arg + 1 < argc) && (argv[arg + 1][0] != '-'))
        lowcovDepth     = atoi(argv[++arg]);
      else
        invalid++;

      if (invalid) {
        char *s = new char [1024];
        snprintf(s, 1024, "Too few parameters to -unassembled option.\n");
        err.push_back(s);
      }

    } else if ((strcmp(argv[arg], "-mr") == 0) ||
               (strcmp(argv[arg], "-RL") == 0)) {    //  Deprecated
      minReadLen = atoi(argv[++arg]);
    } else if ((strcmp(argv[arg], "-mo") == 0) ||
               (strcmp(argv[arg], "-el") == 0)) {    //  Deprecated
      minOverlapLen = atoi(argv[++arg]);

    } else if (strcmp(argv[arg], "-mi") == 0) {
      minIntersectLen = atoi(argv[++arg]);
    } else if (strcmp(argv[arg], "-mp") == 0) {
      maxPlacements = atoi(argv[++arg]);

    } else if (strcmp(argv[arg], "-threads") == 0) {
      if ((numThreads = atoi(argv[++arg])) > 0)
        omp_set_num_threads(numThreads);

    } else if (strcmp(argv[arg], "-eg") == 0) {
      erateGraph = atof(argv[++arg]);
    } else if (strcmp(argv[arg], "-eM") == 0) {
      erateMax = atof(argv[++arg]);

    } else if (strcmp(argv[arg], "-ca") == 0) {  //  Edge confused, based on absolute difference
      confusedAbsolute = atoi(argv[++arg]);
    } else if (strcmp(argv[arg], "-cp") == 0) {  //  Edge confused, based on percent difference
      confusedPercent = atof(argv[++arg]);

    } else if (strcmp(argv[arg], "-dg") == 0) {  //  Deviations, graph
      deviationGraph = atof(argv[++arg]);
    } else if (strcmp(argv[arg], "-db") == 0) {  //  Deviations, bubble
      deviationBubble = atof(argv[++arg]);
    } else if (strcmp(argv[arg], "-dr") == 0) {  //  Deviations, repeat
      deviationRepeat = atof(argv[++arg]);

    } else if (strcmp(argv[arg], "-nofilter") == 0) {
      ++arg;
      filterSuspicious = ((arg >= argc) || (strcasestr(argv[arg], "suspicious") == NULL));
      filterHighError  = ((arg >= argc) || (strcasestr(argv[arg], "higherror")  == NULL));
      filterLopsided   = ((arg >= argc) || (strcasestr(argv[arg], "lopsided")   == NULL));
      filterSpur       = ((arg >= argc) || (strcasestr(argv[arg], "spur")       == NULL));
      filterDeadEnds   = ((arg >= argc) || (strcasestr(argv[arg], "deadends")   == NULL));

    } else if (strcmp(argv[arg], "-M") == 0) {
      ovlCacheMemory  = (uint64)(atof(argv[++arg]) * 1024 * 1024 * 1024);

    } else if (strcmp(argv[arg], "-save") == 0) {
      doSave = true;

    } else if (strcmp(argv[arg], "-D") == 0) {
      uint32  opt = 0;
      uint64  flg = 1;
      bool    fnd = false;
      for (arg++; logFileFlagNames[opt]; flg <<= 1, opt++) {
        if (strcasecmp(logFileFlagNames[opt], argv[arg]) == 0) {
          logFileFlags |= flg;
          fnd = true;
        }
      }
      if (strcasecmp("all", argv[arg]) == 0) {
        for (flg=1, opt=0; logFileFlagNames[opt]; flg <<= 1, opt++)
          if (strcasecmp(logFileFlagNames[opt], "stderr") != 0)
            logFileFlags |= flg;
        fnd = true;
      }
      if (strcasecmp("most", argv[arg]) == 0) {
        for (flg=1, opt=0; logFileFlagNames[opt]; flg <<= 1, opt++)
          if ((strcasecmp(logFileFlagNames[opt], "stderr") != 0) &&
              (strcasecmp(logFileFlagNames[opt], "overlapScoring") != 0) &&
              (strcasecmp(logFileFlagNames[opt], "errorProfiles") != 0) &&
              (strcasecmp(logFileFlagNames[opt], "chunkGraph") != 0) &&
              (strcasecmp(logFileFlagNames[opt], "setParentAndHang") != 0))
            logFileFlags |= flg;
        fnd = true;
      }
      if (fnd == false) {
        char *s = new char [1024];
        snprintf(s, 1024, "Unknown '-D' option '%s'.\n", argv[arg]);
        err.push_back(s);
      }

    } else if (strcmp(argv[arg], "-d") == 0) {
      uint32  opt = 0;
      uint64  flg = 1;
      bool    fnd = false;
      for (arg++; logFileFlagNames[opt]; flg <<= 1, opt++) {
        if (strcasecmp(logFileFlagNames[opt], argv[arg]) == 0) {
          logFileFlags &= ~flg;
          fnd = true;
        }
      }
      if (fnd == false) {
        char *s = new char [1024];
        snprintf(s, 1024, "Unknown '-d' option '%s'.\n", argv[arg]);
        err.push_back(s);
      }

    } else {
      char *s = new char [1024];
      snprintf(s, 1024, "Unknown option '%s'.\n", argv[arg]);
      err.push_back(s);
    }

    arg++;
  }

  if (erateGraph    < 0.0)     err.push_back("Invalid overlap error threshold (-eg option); must be at least 0.0.\n");
  if (erateMax      < 0.0)     err.push_back("Invalid overlap error threshold (-eM option); must be at least 0.0.\n");
  if (prefix       == NULL)    err.push_back("No output prefix name (-o option) supplied.\n");
  if (gkpStorePath == NULL)    err.push_back("No gatekeeper store (-G option) supplied.\n");
  if (ovlStorePath == NULL)    err.push_back("No overlap store (-O option) supplied.\n");

  if (err.size() > 0) {
    fprintf(stderr, "usage: %s -o outputName -O ovlStore -G gkpStore -T tigStore\n", argv[0]);
    fprintf(stderr, "\n");
    fprintf(stderr, "  -O         Mandatory path to an ovlStore.\n");
    fprintf(stderr, "  -G         Mandatory path to a gkpStore.\n");
    fprintf(stderr, "  -T         Mandatory path to a tigStore (can exist or not).\n");
    fprintf(stderr, "  -o prefix  Mandatory name for the output files\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Algorithm Options\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  -gs        Genome size in bases.\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  -mr len    Force reads below 'len' bases to be singletons.\n");
    fprintf(stderr, "  -mo len    Ignore overlaps shorter than 'len' bases.\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  -mi len    Create unitigs from contig intersections of at least 'len' bases.\n");
    fprintf(stderr, "  -mp num    Create unitigs from contig intersections with at most 'num' placements.\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  -nofilter [suspicious],[higherror],[lopsided],[spur]\n");
    fprintf(stderr, "             Disable filtering of:\n");
    fprintf(stderr, "               suspicious - reads that have a suspicious lack of overlaps\n");
    fprintf(stderr, "               higherror  - overlaps that have error rates well outside the observed\n");
    fprintf(stderr, "               lopsided   - reads that have unusually asymmetric best overlaps\n");
    fprintf(stderr, "               spur       - reads that have no overlaps on one end\n");
    fprintf(stderr, "             The value supplied to -nofilter must be one word, order and punctuation\n");
    fprintf(stderr, "             do not matter.  The following examples behave the same:\n");
    fprintf(stderr, "                '-nofilter suspicious,higherror'\n");
    fprintf(stderr, "                '-nofilter suspicious-and-higherror'\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  -threads N Use N compute threads during repeat detection.\n");
    fprintf(stderr, "               0 - use OpenMP default (default)\n");
    fprintf(stderr, "               1 - use one thread\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Overlap Selection - an overlap will be considered for use in a unitig under\n");
    fprintf(stderr, "                    the following conditions:\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  When constructing the Best Overlap Graph and Greedy tigs ('g'raph):\n");
    fprintf(stderr, "    -eg 0.020   no more than 0.020 fraction (2.0%%) error   ** DEPRECATED **\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  When loading overlaps, an inflated maximum (to allow reruns with different error rates):\n");
    fprintf(stderr, "    -eM 0.05   no more than 0.05 fraction (5.0%%) error in any overlap loaded into bogart\n");
    fprintf(stderr, "               the maximum used will ALWAYS be at leeast the maximum of the four error rates\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Overlap Storage\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "    -M gb    Use at most 'gb' gigabytes of memory for storing overlaps.\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "    -save    Save the overlap graph to disk, and continue.\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Debugging and Logging\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  -D <name>  enable logging/debugging for a specific component.\n");
    fprintf(stderr, "  -d <name>  disable logging/debugging for a specific component.\n");
    for (uint32 l=0; logFileFlagNames[l]; l++)
      fprintf(stderr, "               %s\n", logFileFlagNames[l]);
    fprintf(stderr, "\n");

    for (uint32 ii=0; ii<err.size(); ii++)
      if (err[ii])
        fputs(err[ii], stderr);

    exit(1);
  }

  fprintf(stderr, "\n");
  fprintf(stderr, "==> PARAMETERS.\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "Resources:\n");
  fprintf(stderr, "  Memory                " F_U64 " GB\n", ovlCacheMemory >> 30);
  fprintf(stderr, "  Compute Threads       %d (%s)\n", omp_get_max_threads(), (numThreads > 0) ? "command line" : "OpenMP default");
  fprintf(stderr, "\n");
  fprintf(stderr, "Lengths:\n");
  fprintf(stderr, "  Minimum read          %u bases\n",     minReadLen);
  fprintf(stderr, "  Minimum overlap       %u bases\n",     minOverlapLen);
  fprintf(stderr, "\n");
  fprintf(stderr, "Overlap Error Rates:\n");
  fprintf(stderr, "  Graph                 %.3f (%.3f%%)\n", erateGraph, erateGraph  * 100);
  fprintf(stderr, "  Max                   %.3f (%.3f%%)\n", erateMax,   erateMax    * 100);
  fprintf(stderr, "\n");
  fprintf(stderr, "Deviations:\n");
  fprintf(stderr, "  Graph                 %.3f\n", deviationGraph);
  fprintf(stderr, "  Bubble                %.3f\n", deviationBubble);
  fprintf(stderr, "  Repeat                %.3f\n", deviationRepeat);
  fprintf(stderr, "\n");
  fprintf(stderr, "Edge Confusion:\n");
  fprintf(stderr, "  Absolute              %d\n",   confusedAbsolute);
  fprintf(stderr, "  Percent               %.4f\n", confusedPercent);
  fprintf(stderr, "\n");
  fprintf(stderr, "Unitig Construction:\n");
  fprintf(stderr, "  Minimum intersection  %u bases\n",     minIntersectLen);
  fprintf(stderr, "  Maxiumum placements   %u positions\n", maxPlacements);
  fprintf(stderr, "\n");
  fprintf(stderr, "Debugging Enabled:\n");

  if (logFileFlags == 0)
    fprintf(stderr, "  (none)\n");

  for (uint64 i=0, j=1; i<64; i++, j<<=1)
    if (logFileFlagSet(j))
      fprintf(stderr, "  %s\n", logFileFlagNames[i]);

  writeStatus("\n");
  writeStatus("==> LOADING AND FILTERING OVERLAPS.\n");
  writeStatus("\n");

  setLogFile(prefix, "filterOverlaps");

  RI = new ReadInfo(gkpStorePath, prefix, minReadLen);
  OC = new OverlapCache(ovlStorePath, prefix, MAX(erateMax, erateGraph), minOverlapLen, ovlCacheMemory, genomeSize, doSave);
  OG = new BestOverlapGraph(erateGraph, deviationGraph, prefix, filterSuspicious, filterHighError, filterLopsided, filterSpur);
  CG = new ChunkGraph(prefix);

  //
  //  Build the initial unitig path from non-contained reads.  The first pass is usually the
  //  only one needed, but occasionally (maybe) we miss reads, so we make an explicit pass
  //  through all reads and place whatever isn't already placed.
  //

  TigVector         contigs(RI->numReads());  //  Both initial greedy tigs and final contigs
  TigVector         unitigs(RI->numReads());  //  The 'final' contigs, split at every intersection in the graph

  writeStatus("\n");
  writeStatus("==> BUILDING GREEDY TIGS.\n");
  writeStatus("\n");

  setLogFile(prefix, "buildGreedy");

  for (uint32 fi=CG->nextReadByChunkLength(); fi>0; fi=CG->nextReadByChunkLength())
    populateUnitig(contigs, fi);

  delete CG;
  CG = NULL;

  breakSingletonTigs(contigs);

  //  populateUnitig() uses only one hang from one overlap to compute the positions of reads.
  //  Once all reads are (approximately) placed, compute positions using all overlaps.

  contigs.optimizePositions(prefix, "buildGreedy");

  //reportOverlaps(contigs, prefix, "buildGreedy");
  reportTigs(contigs, prefix, "buildGreedy", genomeSize);

  //
  //  For future use, remember the reads in contigs.  When we make unitigs, we'll
  //  require that every unitig end with one of these reads -- this will let
  //  us reconstruct contigs from the unitigs.
  //

  for (uint32 fid=1; fid<RI->numReads()+1; fid++)    //  This really should be incorporated
    if (contigs.inUnitig(fid) != 0)                  //  into populateUnitig()
      RI->setBackbone(fid);

  //
  //  Place contained reads.
  //

  writeStatus("\n");
  writeStatus("==> PLACE CONTAINED READS.\n");
  writeStatus("\n");

  setLogFile(prefix, "placeContains");

  //contigs.computeArrivalRate(prefix, "initial");
  contigs.computeErrorProfiles(prefix, "initial");
  contigs.reportErrorProfiles(prefix, "initial");

  placeUnplacedUsingAllOverlaps(contigs, prefix);

  //  Compute positions again.  This fixes issues with contains-in-contains that
  //  tend to excessively shrink reads.  The one case debugged placed contains in
  //  a three read nanopore contig, where one of the contained reads shrank by 10%,
  //  which was enough to swap bgn/end coords when they were computed using hangs
  //  (that is, sum of the hangs was bigger than the placed read length).

  contigs.optimizePositions(prefix, "placeContains");

  //reportOverlaps(contigs, prefix, "placeContains");
  reportTigs(contigs, prefix, "placeContains", genomeSize);

  //
  //  Merge orphans.
  //

  writeStatus("\n");
  writeStatus("==> MERGE ORPHANS.\n");
  writeStatus("\n");

  setLogFile(prefix, "mergeOrphans");

  contigs.computeErrorProfiles(prefix, "unplaced");
  contigs.reportErrorProfiles(prefix, "unplaced");

  mergeOrphans(contigs, deviationBubble);

  //checkUnitigMembership(contigs);
  //reportOverlaps(contigs, prefix, "mergeOrphans");
  reportTigs(contigs, prefix, "mergeOrphans", genomeSize);

  //
  //  Initial construction done.  Classify what we have as assembled or unassembled.
  //

  classifyTigsAsUnassembled(contigs,
                            fewReadsNumber,
                            tooShortLength,
                            spanFraction,
                            lowcovFraction, lowcovDepth);

  //
  //  Generate a new graph using only edges that are compatible with existing tigs.
  //

  writeStatus("\n");
  writeStatus("==> GENERATING ASSEMBLY GRAPH.\n");
  writeStatus("\n");

  setLogFile(prefix, "assemblyGraph");

  contigs.computeErrorProfiles(prefix, "assemblyGraph");
  contigs.reportErrorProfiles(prefix, "assemblyGraph");

  AssemblyGraph *AG = new AssemblyGraph(prefix,
                                        deviationRepeat,
                                        contigs);

  AG->reportReadGraph(contigs, prefix, "initial");

  //
  //  Detect and break repeats.  Annotate each read with overlaps to reads not overlapping in the tig,
  //  project these regions back to the tig, and break unless there is a read spanning the region.
  //

  writeStatus("\n");
  writeStatus("==> BREAK REPEATS.\n");
  writeStatus("\n");

  setLogFile(prefix, "breakRepeats");

  contigs.computeErrorProfiles(prefix, "repeats");
  contigs.reportErrorProfiles(prefix, "repeats");

  vector<confusedEdge>  confusedEdges;

  markRepeatReads(AG, contigs, deviationRepeat, confusedAbsolute, confusedPercent, confusedEdges);

  //checkUnitigMembership(contigs);
  //reportOverlaps(contigs, prefix, "markRepeatReads");
  reportTigs(contigs, prefix, "markRepeatReads", genomeSize);

  //
  //  Cleanup tigs.  Break those that have gaps in them.  Place contains again.  For any read
  //  still unplaced, make it a singleton unitig.
  //

  writeStatus("\n");
  writeStatus("==> CLEANUP MISTAKES.\n");
  writeStatus("\n");

  setLogFile(prefix, "cleanupMistakes");

  splitDiscontinuous(contigs, minOverlapLen);
  promoteToSingleton(contigs);

  if (filterDeadEnds) {
    dropDeadEnds(AG, contigs);
    splitDiscontinuous(contigs, minOverlapLen);
    promoteToSingleton(contigs);
  }

  writeStatus("\n");
  writeStatus("==> CLEANUP GRAPH.\n");
  writeStatus("\n");

  AG->rebuildGraph(contigs);
  AG->filterEdges(contigs);

  writeStatus("\n");
  writeStatus("==> GENERATE OUTPUTS.\n");
  writeStatus("\n");

  setLogFile(prefix, "generateOutputs");

  //checkUnitigMembership(contigs);
  reportOverlaps(contigs, prefix, "final");
  reportTigs(contigs, prefix, "final", genomeSize);

  AG->reportReadGraph(contigs, prefix, "final");

  delete AG;
  AG = NULL;

  //
  //  unitigSource:
  //
  //  We want some way of tracking unitigs that came from the same contig.  Ideally,
  //  we'd be able to emit only the edges that would join unitigs into the original
  //  contig, but it's complicated by containments.  For example:
  //
  //    [----------------------------------]   CONTIG
  //    -------------                          UNITIG
  //              --------------------------   UNITIG
  //                         -------           UNITIG
  //
  //  So, instead, we just remember the set of unitigs that were created from each
  //  contig, and assume that any edge between those unitigs represents the contig.
  //  Which it totally doesn't -- any repeat in the contig collapses -- but is a
  //  good first attempt.
  //

  vector<tigLoc>  unitigSource;

  //  The graph must come first, to find circular contigs.

  reportTigGraph(contigs, unitigSource, prefix, "contigs");

  setParentAndHang(contigs);
  writeTigsToStore(contigs, prefix, "ctg", true);

  setLogFile(prefix, "tigGraph");

  writeStatus("\n");
  writeStatus("==> GENERATE UNITIGS.\n");
  writeStatus("\n");

  setLogFile(prefix, "generateUnitigs");

  contigs.computeErrorProfiles(prefix, "generateUnitigs");
  contigs.reportErrorProfiles(prefix, "generateUnitigs");

  createUnitigs(contigs, unitigs, minIntersectLen, maxPlacements, confusedEdges, unitigSource);

  splitDiscontinuous(unitigs, minOverlapLen, unitigSource);

  reportTigGraph(unitigs, unitigSource, prefix, "unitigs");

  setParentAndHang(unitigs);
  writeTigsToStore(unitigs, prefix, "utg", true);

  //
  //  Tear down bogart.
  //

  //  How bizarre.  Human regression of 2017-07-28-2128 deadlocked (apparently) when deleting OC.
  //  It had 31 threads in futex_wait, thread 1 was in delete of the second block of data.  CPU
  //  usage was 100% IIRC.  Reproducable, at least twice, possibly three times.  setLogFilePrefix
  //  was moved before the deletes in hope that it'll close down threads.  Certainly, it should
  //  close thread output files from createUnitigs.

  setLogFile(prefix, NULL);    //  Close files.
  omp_set_num_threads(1);      //  Hopefully kills off other threads.

  delete CG;
  delete OG;
  delete OC;
  delete RI;

  writeStatus("\n");
  writeStatus("Bye.\n");

  return(0);
}
