//EvaluateDoseVolumeHistograms.cc - A part of DICOMautomaton 2018. Written by hal clark.

#include <iostream>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <string>    
#include <vector>
#include <set> 
#include <map>
#include <unordered_map>
#include <list>
#include <functional>
#include <thread>
#include <array>
#include <mutex>
#include <limits>
#include <cmath>
#include <regex>

#include <getopt.h>           //Needed for 'getopts' argument parsing.
#include <cstdlib>            //Needed for exit() calls.
#include <utility>            //Needed for std::pair.
#include <algorithm>
#include <experimental/optional>

#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>

#include <boost/interprocess/sync/named_mutex.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>

#include "YgorMisc.h"         //Needed for FUNCINFO, FUNCWARN, FUNCERR macros.
#include "YgorMath.h"         //Needed for vec3 class.
#include "YgorMathPlottingGnuplot.h" //Needed for YgorMathPlottingGnuplot::*.
#include "YgorMathChebyshev.h" //Needed for cheby_approx class.
#include "YgorStats.h"        //Needed for Stats:: namespace.
#include "YgorFilesDirs.h"    //Needed for Does_File_Exist_And_Can_Be_Read(...), etc..
#include "YgorContainers.h"   //Needed for bimap class.
#include "YgorPerformance.h"  //Needed for YgorPerformance_dt_from_last().
#include "YgorAlgorithms.h"   //Needed for For_Each_In_Parallel<..>(...)
#include "YgorArguments.h"    //Needed for ArgumentHandler class.
#include "YgorString.h"       //Needed for GetFirstRegex(...)
#include "YgorImages.h"
#include "YgorImagesIO.h"
#include "YgorImagesPlotting.h"

#include "Explicator.h"       //Needed for Explicator class.

#include "../Structs.h"
#include "../BED_Conversion.h"
#include "../Contour_Collection_Estimates.h"

#include "../YgorImages_Functors/Grouping/Misc_Functors.h"

#include "../YgorImages_Functors/Processing/DCEMRI_AUC_Map.h"
#include "../YgorImages_Functors/Processing/DCEMRI_S0_Map.h"
#include "../YgorImages_Functors/Processing/DCEMRI_T1_Map.h"
#include "../YgorImages_Functors/Processing/Highlight_ROI_Voxels.h"
#include "../YgorImages_Functors/Processing/Kitchen_Sink_Analysis.h"
#include "../YgorImages_Functors/Processing/IVIMMRI_ADC_Map.h"
#include "../YgorImages_Functors/Processing/Time_Course_Slope_Map.h"
#include "../YgorImages_Functors/Processing/CT_Perfusion_Clip_Search.h"
#include "../YgorImages_Functors/Processing/CT_Perf_Pixel_Filter.h"
#include "../YgorImages_Functors/Processing/CT_Convert_NaNs_to_Air.h"
#include "../YgorImages_Functors/Processing/Min_Pixel_Value.h"
#include "../YgorImages_Functors/Processing/Max_Pixel_Value.h"
#include "../YgorImages_Functors/Processing/CT_Reasonable_HU_Window.h"
#include "../YgorImages_Functors/Processing/Slope_Difference.h"
#include "../YgorImages_Functors/Processing/Centralized_Moments.h"
#include "../YgorImages_Functors/Processing/Logarithmic_Pixel_Scale.h"
#include "../YgorImages_Functors/Processing/Per_ROI_Time_Courses.h"
#include "../YgorImages_Functors/Processing/DBSCAN_Time_Courses.h"
#include "../YgorImages_Functors/Processing/In_Image_Plane_Bilinear_Supersample.h"
#include "../YgorImages_Functors/Processing/In_Image_Plane_Bicubic_Supersample.h"
#include "../YgorImages_Functors/Processing/In_Image_Plane_Pixel_Decimate.h"
#include "../YgorImages_Functors/Processing/Cross_Second_Derivative.h"
#include "../YgorImages_Functors/Processing/Orthogonal_Slices.h"

#include "../YgorImages_Functors/Transform/DCEMRI_C_Map.h"
#include "../YgorImages_Functors/Transform/DCEMRI_Signal_Difference_C.h"
#include "../YgorImages_Functors/Transform/CT_Perfusion_Signal_Diff.h"
#include "../YgorImages_Functors/Transform/DCEMRI_S0_Map_v2.h"
#include "../YgorImages_Functors/Transform/DCEMRI_T1_Map_v2.h"
#include "../YgorImages_Functors/Transform/Pixel_Value_Histogram.h"
#include "../YgorImages_Functors/Transform/Subtract_Spatially_Overlapping_Images.h"

#include "../YgorImages_Functors/Compute/Per_ROI_Time_Courses.h"
#include "../YgorImages_Functors/Compute/Contour_Similarity.h"
#include "../YgorImages_Functors/Compute/AccumulatePixelDistributions.h"

#include "EvaluateDoseVolumeHistograms.h"



std::list<OperationArgDoc> OpArgDocEvaluateDoseVolumeHistograms(void){
    std::list<OperationArgDoc> out;


    out.emplace_back();
    out.back().name = "OutFileName";
    out.back().desc = "A filename (or full path) in which to append the histogram data generated by this routine."
                      " The format is a two-column data file suitable for plotting. A short header"
                      " separates entries. Leave empty to dump to generate a unique temporary file.";
    out.back().default_val = "";
    out.back().expected = true;
    out.back().examples = { "", "/tmp/somefile", "localfile.dat", "derivative_data.dat" };
    out.back().mimetype = "text/plain";


    out.emplace_back();
    out.back().name = "NormalizedROILabelRegex";
    out.back().desc = "A regex matching the ROI labels/names to consider. The default will match"
                      " all available ROIs. Be aware that input spaces are trimmed to a single space."
                      " If your ROI name has more than two sequential spaces, use regex to avoid them."
                      " All ROIs have to match the single regex, so use the 'or' token if needed."
                      " Regex is case insensitive and uses extended POSIX syntax.";
    out.back().default_val = ".*";
    out.back().expected = true;
    out.back().examples = { ".*", ".*Body.*", "Body", "Gross_Liver",
                            R"***(.*Left.*Parotid.*|.*Right.*Parotid.*|.*Eye.*)***",
                            R"***(Left Parotid|Right Parotid)***" };

    out.emplace_back();
    out.back().name = "ROILabelRegex";
    out.back().desc = "A regex matching the ROI labels/names to consider. The default will match"
                      " all available ROIs. Be aware that input spaces are trimmed to a single space."
                      " If your ROI name has more than two sequential spaces, use regex to avoid them."
                      " All ROIs have to match the single regex, so use the 'or' token if needed."
                      " Regex is case insensitive and uses extended POSIX syntax.";
    out.back().default_val = ".*";
    out.back().expected = true;
    out.back().examples = { ".*", ".*body.*", "body", "Gross_Liver",
                            R"***(.*left.*parotid.*|.*right.*parotid.*|.*eyes.*)***",
                            R"***(left_parotid|right_parotid)***" };

    out.emplace_back();
    out.back().name = "dDose";
    out.back().desc = "The (fixed) bin width, in units of dose.";
    out.back().default_val = "2.0";
    out.back().expected = true;
    out.back().examples = { "0.1", "0.5", "2.0", "5.0", "10", "50" };


    out.emplace_back();
    out.back().name = "UserComment";
    out.back().desc = "A string that will be inserted into the output file which will simplify merging output"
                      " with differing parameters, from different sources, or using sub-selections of the data."
                      " If left empty, the column will be omitted from the output.";
    out.back().default_val = "";
    out.back().expected = false;
    out.back().examples = { "Using XYZ", "Patient treatment plan C" };

    return out;
}



Drover EvaluateDoseVolumeHistograms(Drover DICOM_data, OperationArgPkg OptArgs, std::map<std::string,std::string> /*InvocationMetadata*/, std::string FilenameLex){

    // This operation evaluates dose-volume histograms for the selected ROI(s). 
    //
    // Note: This routine generates cumulative DVHs with absolute dose on the x-axis and fractional volume on the y-axis.
    //
    // Note: This routine will naively treat voxels of different size with the same weighting rather than weighting each
    //       voxel using its spatial extent. This is done to improve precision and reduce numerical issues. If
    //       necessary, resample images to have uniform spatial extent.
    //
    // Note: This routine uses image_arrays so convert dose_arrays beforehand.
    //
    // Note: This routine will combine spatially-overlapping images by summing voxel intensities. It will not
    //       combine separate image_arrays though. If needed, you'll have to perform a meld on them beforehand.
    //

    //---------------------------------------------- User Parameters --------------------------------------------------
    auto OutFilename = OptArgs.getValueStr("OutFileName").value();
    const auto ROILabelRegex = OptArgs.getValueStr("ROILabelRegex").value();
    const auto NormalizedROILabelRegex = OptArgs.getValueStr("NormalizedROILabelRegex").value();
    const auto dD = std::stod(OptArgs.getValueStr("dDose").value());

    const auto UserComment = OptArgs.getValueStr("UserComment");

    //-----------------------------------------------------------------------------------------------------------------
    const auto theregex = std::regex(ROILabelRegex, std::regex::icase | std::regex::nosubs | std::regex::optimize | std::regex::extended);
    const auto thenormalizedregex = std::regex(NormalizedROILabelRegex, std::regex::icase | std::regex::nosubs | std::regex::optimize | std::regex::extended);

    Explicator X(FilenameLex);


    //Merge the image arrays if necessary.
    if(DICOM_data.image_data.empty()){
        throw std::invalid_argument("This routine requires at least one image array. Cannot continue");
    }

    auto img_arr_ptr = DICOM_data.image_data.front();
    if(img_arr_ptr == nullptr){
        throw std::runtime_error("Encountered a nullptr when expecting a valid Image_Array ptr.");
    }else if(img_arr_ptr->imagecoll.images.empty()){
        throw std::runtime_error("Encountered a Image_Array with valid images -- no images found.");
    }

    //Stuff references to all contours into a list. Remember that you can still address specific contours through
    // the original holding containers (which are not modified here).
    std::list<std::reference_wrapper<contour_collection<double>>> cc_all;
    for(auto & cc : DICOM_data.contour_data->ccs){
        auto base_ptr = reinterpret_cast<contour_collection<double> *>(&cc);
        cc_all.push_back( std::ref(*base_ptr) );
    }

    //Whitelist contours using the provided regex.
    auto cc_ROIs = cc_all;
    cc_ROIs.remove_if([=](std::reference_wrapper<contour_collection<double>> cc) -> bool {
                   const auto ROINameOpt = cc.get().contours.front().GetMetadataValueAs<std::string>("ROIName");
                   const auto ROIName = ROINameOpt.value_or("");
                   return !(std::regex_match(ROIName,theregex));
    });
    cc_ROIs.remove_if([=](std::reference_wrapper<contour_collection<double>> cc) -> bool {
                   const auto ROINameOpt = cc.get().contours.front().GetMetadataValueAs<std::string>("NormalizedROIName");
                   const auto ROIName = ROINameOpt.value_or("");
                   return !(std::regex_match(ROIName,thenormalizedregex));
    });

    if(cc_ROIs.empty()){
        throw std::invalid_argument("No contours selected. Cannot continue.");
    }

    std::string patient_ID;
    if( auto o = cc_ROIs.front().get().contours.front().GetMetadataValueAs<std::string>("PatientID") ){
        patient_ID = o.value();
    }else if( auto o = cc_ROIs.front().get().contours.front().GetMetadataValueAs<std::string>("StudyInstanceUID") ){
        patient_ID = o.value();
    }else{
        patient_ID = "unknown_person";
    }

    //Accumulate the voxel intensity distributions.
    AccumulatePixelDistributionsUserData ud;
    if(!img_arr_ptr->imagecoll.Compute_Images( AccumulatePixelDistributions, { },
                                               cc_ROIs, &ud )){
        throw std::runtime_error("Unable to accumulate pixel distributions.");
    }

    std::map<std::string, std::map<double,double>> dvhs;
    {
        for(const auto &av : ud.accumulated_voxels){
            const auto lROIname = av.first;

            if(av.second.empty()){
                FUNCWARN("Asked to compute DVH when no voxels appear to have any dose. This is physically possible, but please be sure it is what you expected");
                //Could be due to:
                // -contours being too small (much smaller than voxel size).
                // -dose and contours not aligning properly. Maybe due to incorrect offsets/rotations/coordinate system?
                // -dose/contours not being present. Maybe accidentally?
                dvhs[lROIname][0.0] = 0.0; //Nothing over 0Gy is delivered to any voxel (0% of volume).
            }else{
                const auto D_min = std::min( 0.0, Stats::Min(av.second) );
                const double N = static_cast<double>(av.second.size());

                for(size_t i = 0;  ; ++i){
                    const double test_dose = D_min + (dD * i);
                    double cumulative = 0.0;
                    for(double i : av.second){
                        if(i >= test_dose) cumulative += 1.0;
                    }

                    const auto dose = test_dose; // No scaling.
                    const auto frac = cumulative / N;
                    dvhs[lROIname][dose] = frac;
                    if(cumulative < 1.0) break;
                }
            }
        }
    }


    //Report the findings. 
    FUNCINFO("Attempting to claim a mutex");
    try{
        //File-based locking is used so this program can be run over many patients concurrently.
        //
        //Try open a named mutex. Probably created in /dev/shm/ if you need to clear it manually...
        boost::interprocess::named_mutex mutex(boost::interprocess::open_or_create,
                                               "dicomautomaton_operation_evaluatendvhs_mutex");
        boost::interprocess::scoped_lock<boost::interprocess::named_mutex> lock(mutex);

        if(OutFilename.empty()){
            OutFilename = Get_Unique_Sequential_Filename("/tmp/dicomautomaton_evaluatendvhs_", 6, ".dat");
        }
        std::fstream FO(OutFilename, std::fstream::out | std::fstream::app);
        if(!FO){
            throw std::runtime_error("Unable to open file for reporting derivative data. Cannot continue.");
        }
        for(const auto &av : ud.accumulated_voxels){
            const auto lROIname = av.first;

            if(UserComment){
                FO << "# UserComment: " << UserComment.value() << std::endl;
            }
            FO << "# PatientID: "         << patient_ID << std::endl;
            FO << "# ROIname: "           << lROIname << std::endl;
            FO << "# NormalizedROIname: " << X(lROIname) << std::endl;
            FO << "# DoseMin: "           << Stats::Min(av.second) << std::endl;
            FO << "# DoseMax: "           << Stats::Max(av.second) << std::endl;
            FO << "# VoxelCount: "        << av.second.size() << std::endl;
            for(const auto &p : dvhs[lROIname]){
                FO << p.first << " " << p.second << "\n";
            }
        }
        FO.flush();
        FO.close();

    }catch(const std::exception &e){
        FUNCERR("Unable to write to output dose-volume histogram file: '" << e.what() << "'");
    }

    return std::move(DICOM_data);
}
