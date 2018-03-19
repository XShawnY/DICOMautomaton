//Subsegment_ComputeDose_VanLuijk.cc - A part of DICOMautomaton 2015, 2016. Written by hal clark.

#include <boost/interprocess/creation_tags.hpp>
#include <boost/interprocess/sync/named_mutex.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <cmath>
#include <cstdlib>            //Needed for exit() calls.
#include <exception>
#include <experimental/any>
#include <experimental/optional>
#include <fstream>
#include <functional>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <regex>
#include <stdexcept>
#include <string>    
#include <utility>            //Needed for std::pair.
#include <vector>

#include "../Dose_Meld.h"
#include "../Structs.h"
#include "../YgorImages_Functors/Compute/AccumulatePixelDistributions.h"
#include "Explicator.h"       //Needed for Explicator class.
#include "Subsegment_ComputeDose_VanLuijk.h"
#include "YgorFilesDirs.h"    //Needed for Does_File_Exist_And_Can_Be_Read(...), etc..
#include "YgorImages.h"
#include "YgorMath.h"         //Needed for vec3 class.
#include "YgorMisc.h"         //Needed for FUNCINFO, FUNCWARN, FUNCERR macros.
#include "YgorStats.h"        //Needed for Stats:: namespace.
#include "YgorString.h"       //Needed for GetFirstRegex(...)



std::list<OperationArgDoc> OpArgDocSubsegment_ComputeDose_VanLuijk(void){
    std::list<OperationArgDoc> out;

    out.emplace_back();
    out.back().name = "AreaDataFileName";
    out.back().desc = "A filename (or full path) in which to append sub-segment areaa data generated by this routine."
                      " The format is CSV. Note that if a sub-segment has zero area or does not exist, no area will"
                      " be printed. You'll have to manually add sub-segments with zero area as needed if this info"
                      " is relevant to you (e.g., if you are deriving a population average)."
                      " Leave empty to NOT dump anything.";
    out.back().default_val = "";
    out.back().expected = true;
    out.back().examples = { "", "/tmp/somefile", "localfile.csv", "area_data.csv" };
    out.back().mimetype = "text/csv";


    out.emplace_back();
    out.back().name = "DerivativeDataFileName";
    out.back().desc = "A filename (or full path) in which to append derivative data generated by this routine."
                      " The format is CSV. Leave empty to dump to generate a unique temporary file.";
    out.back().default_val = "";
    out.back().expected = true;
    out.back().examples = { "", "/tmp/somefile", "localfile.csv", "derivative_data.csv" };
    out.back().mimetype = "text/csv";


    out.emplace_back();
    out.back().name = "DistributionDataFileName";
    out.back().desc = "A filename (or full path) in which to append raw distribution data generated by this routine."
                      " The format is one line of description followed by one line for the distribution;"
                      " pixel intensities are listed with a single space between elements; the descriptions contain"
                      " the patient ID, ROIName, and subsegment description (guaranteed) and possibly various other"
                      " data afterward. Leave empty to NOT dump anything.";
    out.back().default_val = "";
    out.back().expected = true;
    out.back().examples = { "", "/tmp/somefile", "localfile.csv", "distributions.data" };
    out.back().mimetype = "text/csv";


    out.emplace_back();
    out.back().name = "NormalizedROILabelRegex";
    out.back().desc = "A regex matching ROI labels/names to consider. The default will match"
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
    out.back().name = "PlanarOrientation";
    out.back().desc = "A string instructing how to orient the cleaving planes."
                      " Currently only 'AxisAligned' (i.e., align with the image/dose grid row and column"
                      " unit vectors) and 'StaticOblique' (i.e., same as AxisAligned but rotated 22.5 degrees"
                      " to reduce colinearity, which sometimes improves sub-segment area consistency).";
    out.back().default_val = "AxisAligned";
    out.back().expected = true;
    out.back().examples = { "AxisAligned", "StaticOblique" };


    out.emplace_back();
    out.back().name = "ReplaceAllWithSubsegment";
    out.back().desc = "Keep the sub-segment and remove any existing contours from the original ROIs."
                      " This is most useful for further processing, such as nested sub-segmentation."
                      " Note that sub-segment contours currently have identical metadata to their"
                      " parent contours.";
    out.back().default_val = "false";
    out.back().expected = true;
    out.back().examples = { "true", "false" };


    out.emplace_back();
    out.back().name = "RetainSubsegment";
    out.back().desc = "Keep the sub-segment as part of the original ROIs. The contours are appended to"
                      " the original ROIs, but the contour ROIName and NormalizedROIName are set to the"
                      " argument provided. (If no argument is provided, sub-segments are not retained.)"
                      " This is most useful for inspection of sub-segments. Note"
                      " that sub-segment contours currently have identical metadata to their"
                      " parent contours, except they are renamed accordingly.";
    out.back().default_val = "";
    out.back().expected = true;
    out.back().examples = { "subsegment_01", "subsegment_02", "selected_subsegment" };


    out.emplace_back();
    out.back().name = "ROILabelRegex";
    out.back().desc = "A regex matching ROI labels/names to consider. The default will match"
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
    out.back().name = "SubsegMethod";
    out.back().desc = "The method to use for sub-segmentation. Nested sub-segmentation should almost"
                      " always be preferred unless you know what you're doing. It should be faster too."
                      " The compound method was used in the van Luijk paper, but it is known to have"
                      " serious problems.";
    out.back().default_val = "nested";
    out.back().expected = true;
    out.back().examples = { "nested", "compound" };


    out.emplace_back();
    out.back().name = "XSelection";
    out.back().desc = "(See ZSelection description.) The \"X\" direction is defined in terms of movement"
                      " on an image when the row number increases. This is generally VERTICAL and DOWNWARD."
                      " All selections are defined in terms of the original ROIs.";
    out.back().default_val = "1.0;0.0";
    out.back().expected = true;
    out.back().examples = { "0.50;0.50", "0.50;0.0", "0.30;0.0", "0.30;0.70" };


    out.emplace_back();
    out.back().name = "YSelection";
    out.back().desc = "(See ZSelection description.) The \"Y\" direction is defined in terms of movement"
                      " on an image when the column number increases. This is generally HORIZONTAL and RIGHTWARD."
                      " All selections are defined in terms of the original ROIs.";
    out.back().default_val = "1.0;0.0";
    out.back().expected = true;
    out.back().examples = { "0.50;0.50", "0.50;0.0", "0.30;0.0", "0.30;0.70" };

    
    out.emplace_back();
    out.back().name = "ZSelection";
    out.back().desc = "The thickness and offset defining the single, continuous extent of the sub-segmentation in"
                      " terms of the fractional area remaining above a plane. The planes define the portion extracted"
                      " and are determined such that sub-segmentation will give the desired fractional planar areas."
                      " The numbers specify the thickness and offset from the bottom of the ROI volume to the bottom"
                      " of the extent. The 'upper' direction is take from the contour plane orientation and assumed to"
                      " be positive if pointing toward the positive-z direction. Only a single 3D selection can be made"
                      " per operation invocation. Sub-segmentation can be performed in transverse (\"Z\"), row_unit"
                      " (\"X\"), and column_unit (\"Y\") directions (in that order)."
                      " All selections are defined in terms of the original ROIs."
                      " Note that it is possible to perform nested sub-segmentation"
                      " (including passing along the original contours) by opting to"
                      " replace the original ROI contours with this sub-segmentation and invoking this operation"
                      " again with the desired sub-segmentation."
                      " If you want the middle 50\% of an ROI, specify '0.50;0.25'."
                      " If you want the upper 50\% then specify '0.50;0.50'."
                      " If you want the lower 50\% then specify '0.50;0.0'."
                      " If you want the upper 30\% then specify '0.30;0.70'."
                      " If you want the lower 30\% then specify '0.30;0.70'.";
    out.back().default_val = "1.0;0.0";
    out.back().expected = true;
    out.back().examples = { "0.50;0.50", "0.50;0.0", "0.30;0.0", "0.30;0.70" };
    
    return out;
}



Drover Subsegment_ComputeDose_VanLuijk(Drover DICOM_data, OperationArgPkg OptArgs, std::map<std::string,std::string> /*InvocationMetadata*/, std::string FilenameLex){

    //---------------------------------------------- User Parameters --------------------------------------------------
    auto AreaDataFileName = OptArgs.getValueStr("AreaDataFileName").value();
    auto DerivativeDataFileName = OptArgs.getValueStr("DerivativeDataFileName").value();
    auto DistributionDataFileName = OptArgs.getValueStr("DistributionDataFileName").value();
    const auto PlanarOrientation = OptArgs.getValueStr("PlanarOrientation").value();
    const auto ReplaceAllWithSubsegmentStr = OptArgs.getValueStr("ReplaceAllWithSubsegment").value();
    const auto RetainSubsegment = OptArgs.getValueStr("RetainSubsegment").value();
    const auto ROILabelRegex = OptArgs.getValueStr("ROILabelRegex").value();
    const auto NormalizedROILabelRegex = OptArgs.getValueStr("NormalizedROILabelRegex").value();
    const auto SubsegMethodReq = OptArgs.getValueStr("SubsegMethod").value();
    const auto XSelectionStr = OptArgs.getValueStr("XSelection").value();
    const auto YSelectionStr = OptArgs.getValueStr("YSelection").value();
    const auto ZSelectionStr = OptArgs.getValueStr("ZSelection").value();

    //-----------------------------------------------------------------------------------------------------------------
    const auto theregex = std::regex(ROILabelRegex, std::regex::icase | std::regex::nosubs | std::regex::optimize | std::regex::extended);
    const auto TrueRegex = std::regex("^tr?u?e?$", std::regex::icase | std::regex::nosubs | std::regex::optimize | std::regex::extended);
    const auto thenormalizedregex = std::regex(NormalizedROILabelRegex, std::regex::icase | std::regex::nosubs | std::regex::optimize | std::regex::extended);
    const auto SubsegMethodCompound = std::regex("Compound", std::regex::icase | std::regex::nosubs | std::regex::optimize | std::regex::extended);
    const auto SubsegMethodNested = std::regex("Nested", std::regex::icase | std::regex::nosubs | std::regex::optimize | std::regex::extended);

    const auto OrientAxisAligned = std::regex("AxisAligned", std::regex::icase | std::regex::nosubs | std::regex::optimize | std::regex::extended);
    const auto OrientStaticObl = std::regex("StaticOblique", std::regex::icase | std::regex::nosubs | std::regex::optimize | std::regex::extended);

    const auto ReplaceAllWithSubsegment = std::regex_match(ReplaceAllWithSubsegmentStr, TrueRegex);

    const auto XSelectionTokens = SplitStringToVector(XSelectionStr, ';', 'd');
    const auto YSelectionTokens = SplitStringToVector(YSelectionStr, ';', 'd');
    const auto ZSelectionTokens = SplitStringToVector(ZSelectionStr, ';', 'd');
    if( (XSelectionTokens.size() != 2) || (YSelectionTokens.size() != 2) || (ZSelectionTokens.size() != 2) ){
        throw std::invalid_argument("The spatial extent selections must consist of exactly two numbers. Cannot continue.");
    }
    const double XSelectionThickness = std::stod(XSelectionTokens.front());
    const double XSelectionOffsetFromBottom = std::stod(XSelectionTokens.back());
    const double YSelectionThickness = std::stod(YSelectionTokens.front());
    const double YSelectionOffsetFromBottom = std::stod(YSelectionTokens.back());
    const double ZSelectionThickness = std::stod(ZSelectionTokens.front());
    const double ZSelectionOffsetFromBottom = std::stod(ZSelectionTokens.back());

    //The bisection routine requires for input the fractional area above the plane. We convert from (thickness,offset)
    // units to the fractional area above the plane for both upper and lower extents.
    const double XSelectionLower = 1.0 - XSelectionOffsetFromBottom;
    const double XSelectionUpper = 1.0 - XSelectionOffsetFromBottom - XSelectionThickness;
    const double YSelectionLower = 1.0 - YSelectionOffsetFromBottom;
    const double YSelectionUpper = 1.0 - YSelectionOffsetFromBottom - YSelectionThickness;
    const double ZSelectionLower = 1.0 - ZSelectionOffsetFromBottom;
    const double ZSelectionUpper = 1.0 - ZSelectionOffsetFromBottom - ZSelectionThickness;

    if(!isininc(0.0,XSelectionLower,1.0) || !isininc(0.0,XSelectionUpper,1.0)){
        FUNCWARN("XSelection is not valid. The selection exceeds [0,1]. Lower and Upper are "
                 << XSelectionLower << " and " << XSelectionUpper << " respectively");
    }else if(!isininc(0.0,YSelectionLower,1.0) || !isininc(0.0,YSelectionUpper,1.0)){
        FUNCWARN("YSelection is not valid. The selection exceeds [0,1]. Lower and Upper are "
                 << YSelectionLower << " and " << YSelectionUpper << " respectively");
    }else if(!isininc(0.0,ZSelectionLower,1.0) || !isininc(0.0,ZSelectionUpper,1.0)){
        FUNCWARN("ZSelection is not valid. The selection exceeds [0,1]. Lower and Upper are "
                 << ZSelectionLower << " and " << ZSelectionUpper << " respectively");
    }

    Explicator X(FilenameLex);

    //Merge the dose arrays if necessary.
    if(DICOM_data.dose_data.empty()){
        throw std::invalid_argument("This routine requires at least one dose image array. Cannot continue");
    }
    DICOM_data.dose_data = Meld_Dose_Data(DICOM_data.dose_data);
    if(DICOM_data.dose_data.size() != 1){
        throw std::invalid_argument("Unable to meld doses into a single dose array. Cannot continue.");
    }

    //auto img_arr_ptr = DICOM_data.image_data.front();
    auto img_arr_ptr = DICOM_data.dose_data.front();

    if(img_arr_ptr == nullptr){
        throw std::runtime_error("Encountered a nullptr when expecting a valid Image_Array or Dose_Array ptr.");
    }else if(img_arr_ptr->imagecoll.images.empty()){
        throw std::runtime_error("Encountered a Image_Array or Dose_Array with valid images -- no images found.");
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
                   const auto ROIName = ROINameOpt.value();
                   return !(std::regex_match(ROIName,theregex));
    });
    cc_ROIs.remove_if([=](std::reference_wrapper<contour_collection<double>> cc) -> bool {
                   const auto ROINameOpt = cc.get().contours.front().GetMetadataValueAs<std::string>("NormalizedROIName");
                   const auto ROIName = ROINameOpt.value();
                   return !(std::regex_match(ROIName,thenormalizedregex));
    });

    if(cc_ROIs.empty()){
        throw std::invalid_argument("No contours selected. Cannot continue.");
    }

    const auto patient_ID = cc_ROIs.front().get().contours.front().GetMetadataValueAs<std::string>("PatientID").value();

    //Get the plane in which the contours are defined on. Since we require a dose array for this routine, and the dose
    // slices and contours must align to make sense, we can pull orientation normals from the slices. Alternatively we
    // can pull an 
    //
    // Note: the planar orientation will point to the next image, not the 'upward' direction. We cannot guarantee the
    // subject will be properly oriented, so the safest thing to do is ensure all patients share the same orientation
    // and then somehow verify (e.g., by inspection) that the sub-segments you think you're selecting are actually being
    // selected. Another idea would be using spatial relationships between known anatomical structures, or using the
    // expected shape to compare ROI slices to figure out orientation. 
    //const auto z_normal = img_arr_ptr->imagecoll.images.front().image_plane().N_0 * -1.0;
    //const auto x_normal = img_arr_ptr->imagecoll.images.front().row_unit;
    //const auto y_normal = img_arr_ptr->imagecoll.images.front().col_unit;

    // Image-axes aligned normals.
    const auto row_normal = img_arr_ptr->imagecoll.images.front().row_unit;
    const auto col_normal = img_arr_ptr->imagecoll.images.front().col_unit;
    const auto ort_normal = img_arr_ptr->imagecoll.images.front().image_plane().N_0 * -1.0;

    vec3<double> x_normal;
    vec3<double> y_normal;
    vec3<double> z_normal;

    // Use the image-axes aligned normals directly. Sub-segmentation might get snagged on voxel rows or columns.
    if(std::regex_match(PlanarOrientation, OrientAxisAligned)){
        x_normal = row_normal;
        y_normal = col_normal;
        z_normal = ort_normal;

    // Try to offset the axes slightly so they don't align perfectly with the voxel grid. (Align along the row and
    // column directions, or align along the diagonals, which can be just as bad.)
    }else if(std::regex_match(PlanarOrientation, OrientStaticObl)){
        x_normal = (row_normal + col_normal * 0.5).unit();
        y_normal = (col_normal - row_normal * 0.5).unit();
        z_normal = (ort_normal - col_normal * 0.5).unit();
        z_normal.GramSchmidt_orthogonalize(x_normal, y_normal);

    }else{
        throw std::invalid_argument("Planar orientations not understood. Cannot continue.");
    }

    //This routine returns a pair of planes that approximately encompass the desired interior volume. The ROIs are not
    // altered. The lower plane is the first element of the pair. This routine can be applied to any contour_collection
    // and the planes can also be applied to any contour_collection.
    const auto bisect_ROIs = [](const contour_collection<double> &ROIs,
                                const vec3<double> &planar_normal,
                                double SelectionLower,
                                double SelectionUpper) -> 
                                    std::pair<plane<double>, plane<double>> {


        // Bisection parameters. It usually converges quickly but can get stuck, so cap the max_iters fairly low.
        const double acceptable_deviation = 0.001; //Deviation from desired_total_area_fraction_above_plane.
        const size_t max_iters = 20; //If the tolerance cannot be reached after this many iters, report the current plane as-is.

        if(ROIs.contours.empty()) throw std::logic_error("Unable to split empty contour collection.");
        size_t iters_taken = 0;
        double final_area_frac = 0.0;

        //Find the lower plane.
        plane<double> lower_plane;
        ROIs.Total_Area_Bisection_Along_Plane(planar_normal,
                                                      SelectionLower,
                                                      acceptable_deviation,
                                                      max_iters,
                                                      &lower_plane,
                                                      &iters_taken,
                                                      &final_area_frac);
        FUNCINFO("Bisection: planar area fraction"
                 << " above LOWER plane with normal: " << planar_normal
                 << " was " << final_area_frac << "."
                 << " Requested: " << SelectionLower << "."
                 << " Iters: " << iters_taken);

        //Find the upper plane.
        plane<double> upper_plane;
        ROIs.Total_Area_Bisection_Along_Plane(planar_normal,
                                                      SelectionUpper,
                                                      acceptable_deviation,
                                                      max_iters,
                                                      &upper_plane,
                                                      &iters_taken,
                                                      &final_area_frac);
        FUNCINFO("Bisection: planar area fraction"
                 << " above UPPER plane with normal: " << planar_normal
                 << " was " << final_area_frac << "."
                 << " Requested: " << SelectionUpper << "."
                 << " Iters: " << iters_taken);

        return std::make_pair(lower_plane, upper_plane);
    };

    const auto subsegment_interior = [](const contour_collection<double> &ROIs,
                                        const std::pair<plane<double>, plane<double>> &planes) ->
                                            contour_collection<double> {
        const plane<double> lower_plane(planes.first);
        const plane<double> upper_plane(planes.second);

        //Implements the sub-segmentation, selecting only the interior portion.
        auto split1 = ROIs.Split_Along_Plane(lower_plane);
        if(split1.size() != 2){
            throw std::logic_error("Expected exactly two groups, above and below plane.");
        }
        auto split2 = split1.back().Split_Along_Plane(upper_plane);
        if(split2.size() != 2){
            throw std::logic_error("Expected exactly two groups, above and below plane.");
        }

        if(false) for(auto & it : split2){ it.Plot(); }

        const contour_collection<double> cc_selection( split2.front() );
        if( cc_selection.contours.empty() ){
            FUNCWARN("Selection contains no contours. Try adjusting your criteria.");
        }
        return cc_selection;
    };

    //Perform the sub-segmentation.
    std::list<contour_collection<double>> cc_selection;
    for(const auto &cc_ref : cc_ROIs){
        if(cc_ref.get().contours.empty()) continue;

        // ---------------------------------- Compound sub-segmentation --------------------------------------
        //Generate all planes using the original contour_collection before sub-segmenting.
        //
        // NOTE: This method results in sub-segments of different volumes depending on the location within the ROI.
        //       Do not use this method unless you know what you're doing.
        if( std::regex_match(SubsegMethodReq,SubsegMethodCompound) ){
            const auto x_planes_pair = bisect_ROIs(cc_ref.get(), x_normal, XSelectionLower, XSelectionUpper);
            const auto y_planes_pair = bisect_ROIs(cc_ref.get(), y_normal, YSelectionLower, YSelectionUpper);
            const auto z_planes_pair = bisect_ROIs(cc_ref.get(), z_normal, ZSelectionLower, ZSelectionUpper);

            //Perform the sub-segmentation.
            contour_collection<double> running(cc_ref.get());
            running = subsegment_interior(running, x_planes_pair);
            running = subsegment_interior(running, y_planes_pair);
            running = subsegment_interior(running, z_planes_pair);
            cc_selection.emplace_back(running);

        // ----------------------------------- Nested sub-segmentation ---------------------------------------
        // Instead of relying on whole-organ sub-segmentation, attempt to fairly partition the *remaining* volume 
        // at each pair of cleaves.
        //
        // NOTE: This method will generate sub-segments with equal volumes (as best possible given the number of slices
        //       if the plane orientations are aligned with the contour planes) and should be preferred over compound
        //       sub-segmentation in almost all cases. It should be faster too.
        }else if( std::regex_match(SubsegMethodReq,SubsegMethodNested) ){
            contour_collection<double> running(cc_ref.get());

            const auto z_planes_pair = bisect_ROIs(running, z_normal, ZSelectionLower, ZSelectionUpper);
            running = subsegment_interior(running, z_planes_pair);

            const auto x_planes_pair = bisect_ROIs(running, x_normal, XSelectionLower, XSelectionUpper);
            running = subsegment_interior(running, x_planes_pair);

            const auto y_planes_pair = bisect_ROIs(running, y_normal, YSelectionLower, YSelectionUpper);
            running = subsegment_interior(running, y_planes_pair);

            cc_selection.emplace_back(running);

        }else{
            throw std::invalid_argument("Subsegmentation method not understood. Cannot continue.");
        }
    }

    //Generate references.
    decltype(cc_ROIs) final_selected_ROI_refs;
    for(auto &cc : cc_selection) final_selected_ROI_refs.push_back( std::ref(cc) );

    //Accumulate the voxel intensity distributions.
    AccumulatePixelDistributionsUserData ud;
    if(!img_arr_ptr->imagecoll.Compute_Images( AccumulatePixelDistributions, { },
                                           final_selected_ROI_refs, &ud )){
        throw std::runtime_error("Unable to accumulate pixel distributions.");
    }

    //Report the findings.
    try{
        //Try open a named mutex. Probably created in /dev/shm/ if you need to clear it manually...
        boost::interprocess::named_mutex mutex(boost::interprocess::open_or_create,
                                               "dicomautomaton_operation_van_luijk_subsegmentation_mutex");
        boost::interprocess::scoped_lock<boost::interprocess::named_mutex> lock(mutex);

        if(DerivativeDataFileName.empty()){
            DerivativeDataFileName = Get_Unique_Sequential_Filename("/tmp/dicomautomaton_subsegment_vanluijk_derivatives_", 6, ".csv");
        }
        std::fstream FO_deriv(DerivativeDataFileName, std::fstream::out | std::fstream::app);
        if(!FO_deriv){
            throw std::runtime_error("Unable to open file for reporting derivative data. Cannot continue.");
        }
        for(const auto &av : ud.accumulated_voxels){
            const auto lROIname = av.first;
            const auto MeanDose = Stats::Mean( av.second );
            const auto MedianDose = Stats::Median( av.second );

            FO_deriv  << "PatientID='" << patient_ID << "',"
                      << "NormalizedROIname='" << X(lROIname) << "',"
                      << "ROIname='" << lROIname << "',"
                      << "MeanDose=" << MeanDose << ","
                      << "MedianDose=" << MedianDose << ","
                      << "VoxelCount=" << av.second.size() << std::endl;
        }
        FO_deriv.flush();
        FO_deriv.close();
     
        if(!AreaDataFileName.empty()){
            std::fstream FO_area(AreaDataFileName, std::fstream::out | std::fstream::app);
            if(!FO_area){
                throw std::runtime_error("Unable to open file for reporting area data. Cannot continue.");
            }

            std::map<std::string, double> Area;

            for(const auto &cc_ref : final_selected_ROI_refs){
                for(const auto &c : cc_ref.get().contours){
                    const auto lROIname = c.GetMetadataValueAs<std::string>("ROIName").value();
                    Area[X(lROIname)] += std::abs(c.Get_Signed_Area());
                }
            }

            for(auto apair : Area){
                FO_area << "PatientID='" << patient_ID << "',"
                        << "NormalizedROIname='" << apair.first << "',"
                        << "Area='" << apair.second << "'" << std::endl;
            }
            FO_area.flush();
            FO_area.close();
        }


        if(!DistributionDataFileName.empty()){
            std::fstream FO_dist(DistributionDataFileName, std::fstream::out | std::fstream::app);
            if(!FO_dist){
                throw std::runtime_error("Unable to open file for reporting distribution data. Cannot continue.");
            }

            for(const auto &av : ud.accumulated_voxels){
                const auto lROIname = av.first;
                FO_dist << "PatientID='" << patient_ID << "' "
                        << "NormalizedROIname='" << X(lROIname) << "' "
                        << "ROIname='" << lROIname << "' " << std::endl;
                for(const auto &d : av.second) FO_dist << d << " ";
                FO_dist << std::endl;
            }
            FO_dist.flush();
            FO_dist.close();
        }

    }catch(const std::exception &e){
        FUNCERR("Unable to write to log files: '" << e.what() << "'");
    }

    //Keep the sub-segment if the user wants it.
    if( !RetainSubsegment.empty() ){
        for(auto &cc : final_selected_ROI_refs){
            cc.get().Insert_Metadata("ROIName", RetainSubsegment);
            cc.get().Insert_Metadata("NormalizedROIName", RetainSubsegment);
            DICOM_data.contour_data->ccs.emplace_back( cc.get() );
        }
    }
    if(ReplaceAllWithSubsegment){
        DICOM_data.contour_data->ccs.clear();
        for(auto &cc : final_selected_ROI_refs){
            DICOM_data.contour_data->ccs.emplace_back( cc.get() );
        }
    }

    return DICOM_data;
}
