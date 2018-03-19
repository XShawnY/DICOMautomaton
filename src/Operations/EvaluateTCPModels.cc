//EvaluateTCPModels.cc - A part of DICOMautomaton 2017. Written by hal clark.

#include <boost/interprocess/creation_tags.hpp>
#include <boost/interprocess/sync/named_mutex.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <cmath>
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

#include "../Contour_Collection_Estimates.h"
#include "../Structs.h"
#include "../YgorImages_Functors/Compute/AccumulatePixelDistributions.h"
#include "EvaluateTCPModels.h"
#include "Explicator.h"       //Needed for Explicator class.
#include "YgorFilesDirs.h"    //Needed for Does_File_Exist_And_Can_Be_Read(...), etc..
#include "YgorImages.h"
#include "YgorMath.h"         //Needed for vec3 class.
#include "YgorMisc.h"         //Needed for FUNCINFO, FUNCWARN, FUNCERR macros.
#include "YgorStats.h"        //Needed for Stats:: namespace.



std::list<OperationArgDoc> OpArgDocEvaluateTCPModels(void){
    std::list<OperationArgDoc> out;


    out.emplace_back();
    out.back().name = "TCPFileName";
    out.back().desc = "A filename (or full path) in which to append TCP data generated by this routine."
                      " The format is CSV. Leave empty to dump to generate a unique temporary file.";
    out.back().default_val = "";
    out.back().expected = true;
    out.back().examples = { "", "/tmp/somefile", "localfile.csv", "derivative_data.csv" };
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
    out.back().name = "Gamma50";
    out.back().desc = "The unitless 'normalized dose-response gradient' or normalized slope of the logistic"
                      " dose-response model at the half-maximum point (e.g., D_50). Informally,"
                      " this parameter controls the steepness of the dose-response curve. (For more"
                      " specific information, consult a standard reference such as 'Basic Clinical Radiobiology'"
                      " 4th Edition by Joiner et al., sections 5.3-5.5.) This parameter is empirically"
                      " fit and not universal. Late endpoints for normal tissues have gamma_50 around 2-6"
                      " whereas gamma_50 nominally varies around 1.5-2.5 for local control of squamous"
                      " cell carcinomas of the head and neck.";
    out.back().default_val = "2.3";
    out.back().expected = true;
    out.back().examples = { "1.5", "2", "2.5", "6" };

    
    out.emplace_back();
    out.back().name = "Dose50";
    out.back().desc = "The dose (in Gray) needed to achieve 50\% probability of local tumour control according to"
                      " an empirical logistic dose-response model (e.g., D_50). Informally, this"
                      " parameter 'shifts' the model along the dose axis. (For more"
                      " specific information, consult a standard reference such as 'Basic Clinical Radiobiology'"
                      " 4th Edition by Joiner et al., sections 5.1-5.3.) This parameter is empirically"
                      " fit and not universal. In 'Quantifying the position and steepness of radiation "
                      " dose-response curves' by Bentzen and Tucker in 1994, D_50 of around 60-65 Gy are reported"
                      " for local control of head and neck cancers (pyriform sinus carcinoma and neck nodes with"
                      " max diameter <= 3cm). Martel et al. report 84.5 Gy in lung.";
    out.back().default_val = "65";
    out.back().expected = true;
    out.back().examples = { "37.9", "52", "60", "65", "84.5" };


    out.emplace_back();
    out.back().name = "EUD_Gamma50";
    out.back().desc = "The unitless 'normalized dose-response gradient' or normalized slope of the gEUD TCP model."
                      " It is defined only for the generalized Equivalent Uniform Dose (gEUD) model."
                      " This is sometimes referred to as the change in TCP for a unit change in dose straddled at"
                      " the TCD_50 dose. It is a counterpart to the Martel model's 'Gamma_50' parameter, but is"
                      " not quite the same."
                      " Okunieff et al. (doi:10.1016/0360-3016(94)00475-Z) computed Gamma50 for tumours in human"
                      " subjects across multiple institutions; they found a median of 0.8 for gross disease and"
                      " a median of 1.5 for microscopic disease. The inter-quartile range was [0.7:1.8] and"
                      " [0.7:2.2] respectively. (Refer to table 3 for site-specific values.) Additionally, "
                      " Gay et al. (doi:10.1016/j.ejmp.2007.07.001) claim that a value of 4.0 for late effects"
                      " a value of 2.0 for tumors in 'are reasonable initial estimates in [our] experience.' Their"
                      " table 2 lists (NTCP) estimates based on the work of Emami (doi:10.1016/0360-3016(91)90171-Y).";
    out.back().default_val = "0.8";
    out.back().expected = true;
    out.back().examples = { "0.8", "1.5" };


    out.emplace_back();
    out.back().name = "EUD_TCD50";
    out.back().desc = "The uniform dose (in Gray) needed to deliver to the tumour to achieve 50\% probability of local"
                      " control. It is defined only for the generalized Equivalent Uniform Dose (gEUD) model."
                      " It is a counterpart to the Martel model's 'Dose_50' parameter, but is not quite the same "
                      " (n.b., TCD_50 is a uniform dose whereas D_50 is more like a per voxel TCP-weighted mean.)"
                      " Okunieff et al. (doi:10.1016/0360-3016(94)00475-Z) computed TCD50 for tumours in human"
                      " subjects across multiple institutions; they found a median of 51.9 Gy for gross disease and"
                      " a median of 37.9 Gy for microscopic disease. The inter-quartile range was "
                      " [38.4:62.8] and [27.0:49.1] respectively. (Refer to table 3 for site-specific values.)"
                      " Gay et al. (doi:10.1016/j.ejmp.2007.07.001) table 2 lists (NTCP) estimates based on the"
                      " work of Emami (doi:10.1016/0360-3016(91)90171-Y) ranging from 18-68 Gy.";
    out.back().default_val = "51.9";
    out.back().expected = true;
    out.back().examples = { "51.9", "37.9" };


    out.emplace_back();
    out.back().name = "EUD_Alpha";
    out.back().desc = "The weighting factor \\alpha that controls the relative weighting of volume and dose"
                      " in the generalized Equivalent Uniform Dose (gEUD) model."
                      " When \\alpha=1, the gEUD is equivalent to the mean; when \\alpha=0, the gEUD is equivalent to"
                      " the geometric mean."
                      " Wu et al. (doi:10.1016/S0360-3016(01)02585-8) claim that for normal tissues, \\alpha can be"
                      " related to the Lyman-Kutcher-Burman (LKB) model volume parameter 'n' via \\alpha=1/n."
                      " Søvik et al. (doi:10.1016/j.ejmp.2007.09.001) found that gEUD is not strongly impacted by"
                      " errors in \\alpha. "
                      " Niemierko et al. ('A generalized concept of equivalent uniform dose. Med Phys 26:1100, 1999)"
                      " generated maximum likelihood estimates for 'several tumors and normal structures' which"
                      " ranged from –13.1 for local control of chordoma tumors to +17.7 for perforation of "
                      " esophagus." 
                      " Gay et al. (doi:10.1016/j.ejmp.2007.07.001) table 2 lists estimates based on the"
                      " work of Emami (doi:10.1016/0360-3016(91)90171-Y) for normal tissues ranging from 1-31."
                      " Brenner et al. (doi:10.1016/0360-3016(93)90189-3) recommend -7.2 for breast cancer, "
                      " -10 for melanoma, and -13 for squamous cell carcinomas. A 2017 presentation by Ontida "
                      " Apinorasethkul claims the tumour range spans [-40:-1] and the organs at risk range "
                      " spans [1:40]. AAPM TG report 166 also provides a listing of recommended values,"
                      " suggesting -10 for PTV and GTV, +1 for parotid, 20 for spinal cord, and 8-16 for"
                      " rectum, bladder, brainstem, chiasm, eye, and optic nerve. Burman (1991) and QUANTEC"
                      " (2010) also provide estimates.";
    out.back().default_val = "-13.0";
    out.back().expected = true;
    out.back().examples = { "-40", "-13.0", "-10", "-7.2", "0.3", "1", "3", "4", "20", "40" };

    out.emplace_back();
    out.back().name = "Fenwick_C";
    out.back().desc = "This parameter describes the degree that superlinear doses are required to control"
                      " large tumours. In other words, as tumour volume grows, a disproportionate amount of"
                      " additional dose is required to maintain the same level of control."
                      " The Fenwick model is semi-empirical, so this number must be fitted or used from"
                      " values reported in the literature. Fenwick et al. 2008"
                      " (doi:10.1016/j.clon.2008.12.011) provide values: 9.58 for local progression free survival"
                      " at 30 months for NSCLC tumours and 5.00 for head-and-neck tumours.";
    out.back().default_val = "9.58";
    out.back().expected = true;
    out.back().examples = { "9.58", "5.00" };

    out.emplace_back();
    out.back().name = "Fenwick_M";
    out.back().desc = "This parameter describes the dose-response steepness in the Fenwick model."
                      " Fenwick et al. 2008 (doi:10.1016/j.clon.2008.12.011) provide values:"
                      " 0.392 for local progression free survival at 30 months for NSCLC tumours and"
                      " 0.280 for head-and-neck tumours.";
    out.back().default_val = "0.392";
    out.back().expected = true;
    out.back().examples = { "0.392", "0.280" };

    out.emplace_back();
    out.back().name = "Fenwick_Vref";
    out.back().desc = "This parameter is the volume (in DICOM units; usually mm^3) of a reference tumour"
                      " (i.e., GTV; primary tumour and"
                      " involved nodes) which the D_{50} are estimated using. In other words, this is a"
                      " 'nominal' tumour volume. Fenwick et al. 2008"
                      " (doi:10.1016/j.clon.2008.12.011) recommend 148'410 mm^3 (i.e., a sphere of"
                      " diameter 6.6 cm). However, an appropriate value depends on the nature of the tumour.";
    out.back().default_val = "148410.0";
    out.back().expected = true;
    out.back().examples = { "148410.0" };

    out.emplace_back();
    out.back().name = "UserComment";
    out.back().desc = "A string that will be inserted into the output file which will simplify merging output"
                      " with differing parameters, from different sources, or using sub-selections of the data."
                      " If left empty, the column will be omitted from the output.";
    out.back().default_val = "";
    out.back().expected = true;
    out.back().examples = { "", "Using XYZ", "Patient treatment plan C" };

    return out;
}



Drover EvaluateTCPModels(Drover DICOM_data, OperationArgPkg OptArgs, std::map<std::string,std::string> /*InvocationMetadata*/, std::string FilenameLex){

    // This operation evaluates a variety of TCP models for each provided ROI. The selected ROI should be the GTV
    // (according to the Fenwick model). Currently the following are implemented:
    //   - The "Martel" model.
    //   - Equivalent Uniform Dose (EUD) TCP.
    //   - The "Fenwick" model for solid tumours.
    //   - ...TODO...
    //
    // Note: Generally these models require dose in 2Gy/fractions equivalents ("EQD2"). You must pre-convert the data
    //       if the RT plan is not already 2Gy/fraction. There is no easy way to ensure this conversion has taken place
    //       or was unnecessary.
    //
    // Note: This routine uses image_arrays so convert dose_arrays beforehand.
    //
    // Note: This routine will combine spatially-overlapping images by summing voxel intensities. So if you have a time
    //       course it may be more sensible to aggregate images in some way (e.g., spatial averaging) prior to calling
    //       this routine.
    //
    // Note: The Fenwick and Martel models share the value of D_{50}. There may be a slight difference in some cases.
    //       Huang et al. 2015 (doi:10.1038/srep18010) used both models and used 84.5 Gy for the Martel model while
    //       using 84.6 Gy for the Fenwick model. (The paper also reported using a Fenwick 'm' of 0.329 whereas the
    //       original report by Fenwick reported 0.392, so I don't think this should be taken as strong evidence of the
    //       equality of D_{50}. However, the difference seems relatively insignificant.)

    //---------------------------------------------- User Parameters --------------------------------------------------
    auto TCPFileName = OptArgs.getValueStr("TCPFileName").value();
    const auto ROILabelRegex = OptArgs.getValueStr("ROILabelRegex").value();
    const auto NormalizedROILabelRegex = OptArgs.getValueStr("NormalizedROILabelRegex").value();

    const auto UserComment = OptArgs.getValueStr("UserComment");

    const auto Gamma50 = std::stod( OptArgs.getValueStr("Gamma50").value() );
    const auto Dose50 = std::stod( OptArgs.getValueStr("Dose50").value() );

    const auto EUD_Gamma50 = std::stod( OptArgs.getValueStr("EUD_Gamma50").value() );
    const auto EUD_TCD50 = std::stod( OptArgs.getValueStr("EUD_TCD50").value() );
    const auto EUD_Alpha = std::stod( OptArgs.getValueStr("EUD_Alpha").value() );

    const auto Fenwick_D50 = Dose50; // Shared with Martel model. There may be a slight difference though.
    const auto Fenwick_C = std::stod( OptArgs.getValueStr("Fenwick_C").value() );
    const auto Fenwick_M = std::stod( OptArgs.getValueStr("Fenwick_M").value() );
    const auto Fenwick_Vref = std::stod( OptArgs.getValueStr("Fenwick_Vref").value() );

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

    //Compute ROI volume for the Fenwick model.
    const double contour_sep = Estimate_Contour_Separation_Multi(cc_ROIs);
    double ROI_V = 0.0;
    for(auto &cc_ref : cc_ROIs){
        ROI_V += cc_ref.get().Slab_Volume(contour_sep, /*IgnoreContourOrientation=*/ true);
    }

    std::string patient_ID;
    if( auto o = cc_ROIs.front().get().contours.front().GetMetadataValueAs<std::string>("PatientID") ){
        patient_ID = o.value();
    }else if( auto o = cc_ROIs.front().get().contours.front().GetMetadataValueAs<std::string>("StudyInstanceUID") ){
        patient_ID = o.value();
    }else{
        patient_ID = "unknown_patient";
    }

    //Accumulate the voxel intensity distributions.
    AccumulatePixelDistributionsUserData ud;
    if(!img_arr_ptr->imagecoll.Compute_Images( AccumulatePixelDistributions, { },
                                               cc_ROIs, &ud )){
        throw std::runtime_error("Unable to accumulate pixel distributions.");
    }

    //Evalute the models.
    std::map<std::string, double> MartelModel;
    std::map<std::string, double> gEUDModel;
    std::map<std::string, double> FenwickModel;
    {
        for(const auto &av : ud.accumulated_voxels){
            const auto lROIname = av.first;

            const auto N = av.second.size();
            const long double V_frac = static_cast<long double>(1) / N; // Fractional volume of a single voxel compared to whole ROI.

            double TCP_Martel = static_cast<long double>(1);
            double TCP_Fenwick = static_cast<long double>(1);

            std::vector<double> gEUD_elements;
            gEUD_elements.reserve(N);
            for(const auto &D_voxel : av.second){
                // Martel model.
                {
                    const long double numer = std::pow(D_voxel, Gamma50*4);
                    const long double denom = std::pow(Dose50, Gamma50*4) + numer;
                    const long double TCP_voxel = numer/denom; // This is a sigmoid curve.
                    TCP_Martel *= std::pow(TCP_voxel, V_frac);
                }
                
                // gEUD model.
                {
                    gEUD_elements.push_back(V_frac * std::pow(D_voxel, EUD_Alpha));
                }

                // Fenwick model.
                {
                    const long double numer = (D_voxel - Fenwick_D50 - Fenwick_C * std::log(ROI_V/Fenwick_Vref));
                    const long double denom = Fenwick_M * D_voxel * std::sqrt(2.0);
                    //Note: the 'normal distribution function Phi(z)' referred to in Fenwick's paper is
                    // (1/sqrt(2pi))*integral(exp(-x*x/2)dx, -inf, z) == 0.5*(1+erf(z/sqrt(2))).
                    const long double TCP_voxel = 0.5*(1.0 + std::erf(numer/denom)); // This is a sigmoid curve.
                    TCP_Fenwick *= std::pow(TCP_voxel, V_frac);
                }

                // ... other models ...
                // ...

            }

            //Post-processing.
            MartelModel[lROIname] = TCP_Martel;
            FenwickModel[lROIname] = TCP_Fenwick;

            {
                const long double gEUD = std::pow( Stats::Sum(gEUD_elements), static_cast<long double>(1) / EUD_Alpha );

                const long double numer = std::pow(gEUD, EUD_Gamma50*4);
                const long double denom = numer + std::pow(EUD_TCD50, EUD_Gamma50*4);
                double TCP_gEUD = numer/denom; // This is a sigmoid curve.

                gEUDModel[lROIname] = TCP_gEUD; 
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
                                               "dicomautomaton_operation_evaluatetcp_mutex");
        boost::interprocess::scoped_lock<boost::interprocess::named_mutex> lock(mutex);

        if(TCPFileName.empty()){
            TCPFileName = Get_Unique_Sequential_Filename("/tmp/dicomautomaton_evaluatetcp_", 6, ".csv");
        }
        const auto FirstWrite = !Does_File_Exist_And_Can_Be_Read(TCPFileName);
        std::fstream FO_tcp(TCPFileName, std::fstream::out | std::fstream::app);
        if(!FO_tcp){
            throw std::runtime_error("Unable to open file for reporting derivative data. Cannot continue.");
        }
        if(FirstWrite){ // Write a CSV header.
            FO_tcp << "UserComment,"
                   << "PatientID,"
                   << "ROIname,"
                   << "NormalizedROIname,"
                   << "TCPMartelModel,"
                   << "TCPgEUDModel,"
                   << "TCPFenwickModel,"
                   << "DoseMean,"
                   << "DoseMedian,"
                   << "DoseStdDev,"
                   << "VoxelCount"
                   << std::endl;
        }
        for(const auto &av : ud.accumulated_voxels){
            const auto lROIname = av.first;
            const auto DoseMean = Stats::Mean( av.second );
            const auto DoseMedian = Stats::Median( av.second );
            const auto DoseStdDev = std::sqrt(Stats::Unbiased_Var_Est( av.second ));
            const auto TCPMartel = MartelModel[lROIname];
            const auto TCPgEUD = gEUDModel[lROIname];
            const auto TCPFenwick = FenwickModel[lROIname];

            FO_tcp  << UserComment.value_or("") << ","
                    << patient_ID        << ","
                    << lROIname          << ","
                    << X(lROIname)       << ","
                    << TCPMartel*100.0   << ","
                    << TCPgEUD*100.0     << ","
                    << TCPFenwick*100.0  << ","
                    << DoseMean          << ","
                    << DoseMedian        << ","
                    << DoseStdDev        << ","
                    << av.second.size()  << std::endl;
        }
        FO_tcp.flush();
        FO_tcp.close();

    }catch(const std::exception &e){
        FUNCERR("Unable to write to output TCP file: '" << e.what() << "'");
    }

    return DICOM_data;
}
