//EQD2Convert.cc - A part of DICOMautomaton 2017. Written by hal clark.

#include <experimental/any>
#include <experimental/optional>
#include <functional>
#include <iterator>
#include <list>
#include <map>
#include <memory>
#include <regex>
#include <stdexcept>
#include <string>    

#include "../Structs.h"
#include "../YgorImages_Functors/Grouping/Misc_Functors.h"
#include "../YgorImages_Functors/Processing/EQD2Conversion.h"
#include "EQD2Convert.h"
#include "YgorImages.h"
#include "YgorMath.h"         //Needed for vec3 class.



std::list<OperationArgDoc> OpArgDocEQD2Convert(void){
    std::list<OperationArgDoc> out;

    // This operation performs a BED-based conversion to a dose-equivalent that would have 2Gy fractions.
    //
    // Note that this operation requires NumberOfFractions and cannot use DosePerFraction.
    // The reasoning is that the DosePerFraction would need to be specified for each individual voxel;
    // the prescription DosePerFraction is NOT the same as voxels outside the PTV.

    out.emplace_back();
    out.back().name = "DoseImageSelection";
    out.back().desc = "Dose images to operate on. Either 'none', 'last', or 'all'.";
    out.back().default_val = "none";
    out.back().expected = true;
    out.back().examples = { "none", "last", "all" };
    
    out.emplace_back();
    out.back().name = "ImageSelection";
    out.back().desc = "Images to operate on. Either 'none', 'last', or 'all'.";
    out.back().default_val = "last";
    out.back().expected = true;
    out.back().examples = { "none", "last", "all" };
    

    out.emplace_back();
    out.back().name = "AlphaBetaRatioNormal";
    out.back().desc = "The value to use for alpha/beta in normal (non-cancerous) tissues."
                      " Generally a value of 3.0 Gy is used. Tissues that are sensitive to fractionation"
                      " may warrant smaller ratios, such as 1.5-3 Gy for cervical central nervous tissues"
                      " and 2.3-4.9 for lumbar central nervous tissues (consult table 8.1, page 107 in: "
                      " Joiner et al., 'Fractionation: the linear-quadratic approach', 4th Ed., 2009,"
                      " in the book 'Basic Clinical Radiobiology', ISBN: 0340929669)."
                      " Note that the selected ROIs denote which tissues are diseased. The remaining tissues are "
                      " considered to be normal.";
    out.back().default_val = "3.0";
    out.back().expected = true;
    out.back().examples = { "2.0", "3.0" };

    out.emplace_back();
    out.back().name = "AlphaBetaRatioTumour";
    out.back().desc = "The value to use for alpha/beta in diseased (tumourous) tissues."
                      " Generally a value of 10.0 is used. Note that the selected ROIs"
                      " denote which tissues are diseased. The remaining tissues are "
                      " considered to be normal.";
    out.back().default_val = "10.0";
    out.back().expected = true;
    out.back().examples = { "10.0" };

    out.emplace_back();
    out.back().name = "NumberOfFractions";
    out.back().desc = "The number of fractions in which a plan was (or will be) delivered."
                      " Decimal fractions are supported to accommodate previous BED conversions.";
    out.back().default_val = "35";
    out.back().expected = true;
    out.back().examples = { "10", "20.5", "35", "40.123" };


    out.emplace_back();
    out.back().name = "PrescriptionDose";
    out.back().desc = "The prescription dose that was (or will be) delivered to the PTV."
                      " Note that this is a theoretical dose since the PTV or CTV will only nominally"
                      " receive this dose. Also note that the specified dose need not exist somewhere"
                      " in the image. It can be purely theoretical to accommodate previous BED"
                      " conversions.";
    out.back().default_val = "70";
    out.back().expected = true;
    out.back().examples = { "15", "22.5", "45.0", "66", "70.001" };


    out.emplace_back();
    out.back().name = "NormalizedROILabelRegex";
    out.back().desc = "A regex matching ROI labels/names to consider as bounding tumourous tissues."
                      " The default will match"
                      " all available ROIs. Be aware that input spaces are trimmed to a single space."
                      " If your ROI name has more than two sequential spaces, use regex to avoid them."
                      " All ROIs have to match the single regex, so use the 'or' token if needed."
                      " Regex is case insensitive and uses extended POSIX syntax.";
    out.back().default_val = ".*";
    out.back().expected = true;
    out.back().examples = { ".*", ".*GTV.*", "PTV66", R"***(.*PTV.*|.*GTV.**)***" };

    out.emplace_back();
    out.back().name = "ROILabelRegex";
    out.back().desc = "A regex matching ROI labels/names to consider as bounding tumourous tissues."
                      "The default will match"
                      " all available ROIs. Be aware that input spaces are trimmed to a single space."
                      " If your ROI name has more than two sequential spaces, use regex to avoid them."
                      " All ROIs have to match the single regex, so use the 'or' token if needed."
                      " Regex is case insensitive and uses extended POSIX syntax.";
    out.back().default_val = ".*";
    out.back().expected = true;
    out.back().examples = { ".*", ".*GTV.*", "PTV66", R"***(.*PTV.*|.*GTV.**)***" };

    return out;
}



Drover EQD2Convert(Drover DICOM_data, 
                           OperationArgPkg OptArgs, 
                           std::map<std::string,std::string> /*InvocationMetadata*/, 
                           std::string /*FilenameLex*/ ){

    EQD2ConversionUserData ud;

    //---------------------------------------------- User Parameters --------------------------------------------------
    ud.AlphaBetaRatioNormal = std::stod(OptArgs.getValueStr("AlphaBetaRatioNormal").value());
    ud.AlphaBetaRatioTumour = std::stod(OptArgs.getValueStr("AlphaBetaRatioTumour").value());

    ud.NumberOfFractions = std::stod(OptArgs.getValueStr("NumberOfFractions").value());
    ud.PrescriptionDose = std::stod(OptArgs.getValueStr("PrescriptionDose").value());

    const auto NormalizedROILabelRegex = OptArgs.getValueStr("NormalizedROILabelRegex").value();
    const auto ROILabelRegex = OptArgs.getValueStr("ROILabelRegex").value();

    const auto DoseImageSelectionStr = OptArgs.getValueStr("DoseImageSelection").value();
    const auto ImageSelectionStr = OptArgs.getValueStr("ImageSelection").value();

    //-----------------------------------------------------------------------------------------------------------------
    const auto theregex = std::regex(ROILabelRegex, std::regex::icase | std::regex::nosubs | std::regex::optimize | std::regex::extended);
    const auto thenormalizedregex = std::regex(NormalizedROILabelRegex, std::regex::icase | std::regex::nosubs | std::regex::optimize | std::regex::extended);

    const auto regex_none = std::regex("no?n?e?$", std::regex::icase | std::regex::nosubs | std::regex::optimize | std::regex::extended);
    const auto regex_last = std::regex("la?s?t?$", std::regex::icase | std::regex::nosubs | std::regex::optimize | std::regex::extended);
    const auto regex_all  = std::regex("al?l?$",   std::regex::icase | std::regex::nosubs | std::regex::optimize | std::regex::extended);

    if( !std::regex_match(DoseImageSelectionStr, regex_none)
    &&  !std::regex_match(DoseImageSelectionStr, regex_last)
    &&  !std::regex_match(DoseImageSelectionStr, regex_all) ){
        throw std::invalid_argument("Dose Image selection is not valid. Cannot continue.");
    }
    if( !std::regex_match(ImageSelectionStr, regex_none)
    &&  !std::regex_match(ImageSelectionStr, regex_last)
    &&  !std::regex_match(ImageSelectionStr, regex_all) ){
        throw std::invalid_argument("Image selection is not valid. Cannot continue.");
    }

    if( ud.PrescriptionDose <= 0.0 ){
        throw std::invalid_argument("PrescriptionDose must be specified (>0.0)");
    }
    if( ud.NumberOfFractions <= 0.0 ){
        throw std::invalid_argument("NumberOfFractions must be specified (>0.0)");
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


    //Image data.
    auto iap_it = DICOM_data.image_data.begin();
    if(false){
    }else if(std::regex_match(ImageSelectionStr, regex_none)){
        iap_it = DICOM_data.image_data.end();
    }else if(std::regex_match(ImageSelectionStr, regex_last)){
        if(!DICOM_data.image_data.empty()) iap_it = std::prev(DICOM_data.image_data.end());
    }
    while(iap_it != DICOM_data.image_data.end()){
        if(!(*iap_it)->imagecoll.Process_Images_Parallel( GroupIndividualImages,
                                                          EQD2Conversion,
                                                          {}, cc_ROIs, &ud )){
            throw std::runtime_error("Unable to convert image_array voxels to EQD2 using the specified ROI(s).");
        }
        ++iap_it;
    }

    //Dose data.
    auto dap_it = DICOM_data.dose_data.begin();
    if(false){
    }else if(std::regex_match(DoseImageSelectionStr, regex_none)){ dap_it = DICOM_data.dose_data.end();
    }else if(std::regex_match(DoseImageSelectionStr, regex_last)){
        if(!DICOM_data.dose_data.empty()) dap_it = std::prev(DICOM_data.dose_data.end());
    }
    while(dap_it != DICOM_data.dose_data.end()){
        if(!(*dap_it)->imagecoll.Process_Images_Parallel( GroupIndividualImages,
                                                          EQD2Conversion,
                                                          {}, cc_ROIs, &ud )){
            throw std::runtime_error("Unable to convert dose_array voxels to EQD2 using the specified ROI(s).");
        }
        ++dap_it;
    }

    return DICOM_data;
}
