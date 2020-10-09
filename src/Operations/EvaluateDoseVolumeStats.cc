//EvaluateDoseVolumeStats.cc - A part of DICOMautomaton 2017. Written by hal clark.

#include <boost/interprocess/creation_tags.hpp>
#include <boost/interprocess/sync/named_mutex.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <cmath>
#include <exception>
#include <any>
#include <optional>
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

#include "../Structs.h"
#include "../Regex_Selectors.h"
#include "../YgorImages_Functors/Compute/AccumulatePixelDistributions.h"
#include "EvaluateDoseVolumeStats.h"
#include "Explicator.h"       //Needed for Explicator class.
#include "YgorFilesDirs.h"    //Needed for Does_File_Exist_And_Can_Be_Read(...), etc..
#include "YgorImages.h"
#include "YgorMath.h"         //Needed for vec3 class.
#include "YgorMisc.h"         //Needed for FUNCINFO, FUNCWARN, FUNCERR macros.
#include "YgorStats.h"        //Needed for Stats:: namespace.



OperationDoc OpArgDocEvaluateDoseVolumeStats(){
    OperationDoc out;
    out.name = "EvaluateDoseVolumeStats";

    out.desc = 
        R"***(This operation evaluates a variety of Dose-Volume statistics. It is geared toward PTV ROIs.)***"
        R"***( Currently the following are implemented:)***"
        R"***( (1) Dose Homogeneity Index: H = (D_{2%} - D_{98%})/D_{median} | over one or more PTVs, where)***"
        R"***( D_{2%} is the maximum dose that covers 2% of the volume of the PTV, and)***"
        R"***( D_{98%} is the minimum dose that covers 98% of the volume of the PTV. )***"
        R"***( (2) Conformity Number: C = V_{T,pres}^{2} / ( V_{T} * V_{pres} ) where)***"
        R"***( V_{T,pres} is the PTV volume receiving at least 95% of the PTV prescription dose,)***"
        R"***( V_{T} is the volume of the PTV, and)***"
        R"***( V_{pres} is volume of all (tissue) voxels receiving at least 95% of the PTV prescription dose.)***";
        
        
    out.notes.emplace_back(
        "This routine will combine spatially-overlapping images by summing voxel intensities. It will not"
        " combine separate image_arrays though. If needed, you'll have to perform a meld on them beforehand."
    );



    out.args.emplace_back();
    out.args.back().name = "OutFileName";
    out.args.back().desc = "A filename (or full path) in which to append dose statistic data generated by this routine."
                           " The format is CSV. Leave empty to dump to generate a unique temporary file.";
    out.args.back().default_val = "";
    out.args.back().expected = true;
    out.args.back().examples = { "", "/tmp/somefile", "localfile.csv", "derivative_data.csv" };
    out.args.back().mimetype = "text/csv";


    out.args.emplace_back();
    out.args.back().name = "PTVPrescriptionDose";
    out.args.back().desc = "The dose prescribed to the PTV of interest (in Gy).";
    out.args.back().default_val = "70";
    out.args.back().expected = true;
    out.args.back().examples = { "50", "66", "70", "82.5" };

    out.args.emplace_back();
    out.args.back() = RCWhitelistOpArgDoc();
    out.args.back().name = "PTVROILabelRegex";
    out.args.back().default_val = ".*";

    out.args.emplace_back();
    out.args.back() = NCWhitelistOpArgDoc();
    out.args.back().name = "PTVNormalizedROILabelRegex";
    out.args.back().default_val = ".*";

    out.args.emplace_back();
    out.args.back() = RCWhitelistOpArgDoc();
    out.args.back().name = "BodyROILabelRegex";
    out.args.back().default_val = ".*";

    out.args.emplace_back();
    out.args.back() = NCWhitelistOpArgDoc();
    out.args.back().name = "BodyNormalizedROILabelRegex";
    out.args.back().default_val = ".*";

    out.args.emplace_back();
    out.args.back().name = "UserComment";
    out.args.back().desc = "A string that will be inserted into the output file which will simplify merging output"
                           " with differing parameters, from different sources, or using sub-selections of the data."
                           " If left empty, the column will be omitted from the output.";
    out.args.back().default_val = "";
    out.args.back().expected = true;
    out.args.back().examples = { "", "Using XYZ", "Patient treatment plan C" };

    return out;
}



Drover EvaluateDoseVolumeStats(Drover DICOM_data,
                               const OperationArgPkg& OptArgs,
                               const std::map<std::string, std::string>& /*InvocationMetadata*/,
                               const std::string& FilenameLex){

    //---------------------------------------------- User Parameters --------------------------------------------------
    auto OutFilename = OptArgs.getValueStr("OutFileName").value();
    const auto PTVROILabelRegex = OptArgs.getValueStr("PTVROILabelRegex").value();
    const auto PTVNormalizedROILabelRegex = OptArgs.getValueStr("PTVNormalizedROILabelRegex").value();

    const auto BodyROILabelRegex = OptArgs.getValueStr("BodyROILabelRegex").value();
    const auto BodyNormalizedROILabelRegex = OptArgs.getValueStr("BodyNormalizedROILabelRegex").value();

    const auto PTVPrescriptionDose = std::stod( OptArgs.getValueStr("PTVPrescriptionDose").value());

    const auto UserComment = OptArgs.getValueStr("UserComment");

    //-----------------------------------------------------------------------------------------------------------------
    const auto theregex_PTV = Compile_Regex(PTVROILabelRegex);
    const auto thenormalizedregex_PTV = Compile_Regex(PTVNormalizedROILabelRegex);

    const auto theregex_Body = Compile_Regex(BodyROILabelRegex);
    const auto thenormalizedregex_Body = Compile_Regex(BodyNormalizedROILabelRegex);

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
    auto cc_PTV_ROIs = cc_all;
    cc_PTV_ROIs.remove_if([=](std::reference_wrapper<contour_collection<double>> cc) -> bool {
                   const auto ROINameOpt = cc.get().contours.front().GetMetadataValueAs<std::string>("ROIName");
                   const auto ROIName = ROINameOpt.value_or("");
                   return !(std::regex_match(ROIName,theregex_PTV));
    });
    cc_PTV_ROIs.remove_if([=](std::reference_wrapper<contour_collection<double>> cc) -> bool {
                   const auto ROINameOpt = cc.get().contours.front().GetMetadataValueAs<std::string>("NormalizedROIName");
                   const auto ROIName = ROINameOpt.value_or("");
                   return !(std::regex_match(ROIName,thenormalizedregex_PTV));
    });

    if(cc_PTV_ROIs.empty()){
        throw std::invalid_argument("No PTV contours selected. Cannot continue.");
    }

    auto cc_Body_ROIs = cc_all;
    cc_Body_ROIs.remove_if([=](std::reference_wrapper<contour_collection<double>> cc) -> bool {
                   const auto ROINameOpt = cc.get().contours.front().GetMetadataValueAs<std::string>("ROIName");
                   const auto ROIName = ROINameOpt.value_or("");
                   return !(std::regex_match(ROIName,theregex_Body));
    });
    cc_Body_ROIs.remove_if([=](std::reference_wrapper<contour_collection<double>> cc) -> bool {
                   const auto ROINameOpt = cc.get().contours.front().GetMetadataValueAs<std::string>("NormalizedROIName");
                   const auto ROIName = ROINameOpt.value_or("");
                   return !(std::regex_match(ROIName,thenormalizedregex_Body));
    });

    if(cc_Body_ROIs.empty()){
        throw std::invalid_argument("No Body contours selected. Cannot continue.");
    }

    std::string patient_ID;
    if( auto o = cc_PTV_ROIs.front().get().contours.front().GetMetadataValueAs<std::string>("PatientID") ){
        patient_ID = o.value();
    }else if( auto o = cc_PTV_ROIs.front().get().contours.front().GetMetadataValueAs<std::string>("StudyInstanceUID") ){
        patient_ID = o.value();
    }else{
        patient_ID = "unknown_person";
    }

    //Accumulate the voxel intensity distributions.
    AccumulatePixelDistributionsUserData ud_PTV;
    if(!img_arr_ptr->imagecoll.Compute_Images( AccumulatePixelDistributions, { },
                                               cc_PTV_ROIs, &ud_PTV )){
        throw std::runtime_error("Unable to accumulate PTV pixel distributions.");
    }
    AccumulatePixelDistributionsUserData ud_Body;
    if(!img_arr_ptr->imagecoll.Compute_Images( AccumulatePixelDistributions, { },
                                               cc_Body_ROIs, &ud_Body )){
        throw std::runtime_error("Unable to accumulate Body pixel distributions.");
    }


    const auto Dpres95 = 0.95 * PTVPrescriptionDose;
    long int N_Body_over_Dpres95 = 0; //We assume all body ROIs are part of a single object.

    //Evalute the models.
    {
        for(auto &av : ud_Body.accumulated_voxels){
            for(const auto &D_voxel : av.second){
                if(D_voxel > Dpres95) ++N_Body_over_Dpres95;
            }
            av.second.clear(); // Reduce memory pressure.
        }
    }

    std::map<std::string, double> HI; // Heterogeneity index.
    std::map<std::string, double> CN; // Conformity number.

    std::map<std::string, long int> N_PTV_over_Dpres95; //We assume all PTV ROIs are distinct.
    {
        for(const auto &av : ud_PTV.accumulated_voxels){
            const auto lROIname = av.first;

            const auto D_02 = Stats::Percentile(av.second, 0.98); // D_02 == 98% dose percentile.
            const auto D_50 = Stats::Percentile(av.second, 0.50);
            const auto D_98 = Stats::Percentile(av.second, 0.02); // D_98 == 2% dose percentile.

            HI[lROIname] = (D_02 - D_98)/D_50;

            long int N_over_Dpres95 = 0;
            for(const auto &D_voxel : av.second){
                if(D_voxel > Dpres95) ++N_over_Dpres95;
            }
            N_PTV_over_Dpres95[lROIname] += N_over_Dpres95;
        }
        for(const auto &av : ud_PTV.accumulated_voxels){
            const auto lROIname = av.first;
            const auto N = av.second.size();
            //const long double V_frac = static_cast<long double>(1) / N; // Fractional volume of a single voxel compared to whole ROI.

            const auto N_T = static_cast<double>(N);
            const auto N_T_pres = static_cast<double>(N_PTV_over_Dpres95[lROIname]);
            const auto N_pres = static_cast<double>(N_Body_over_Dpres95);

            CN[lROIname] = (N_T_pres * N_T_pres) / (N_T * N_pres);
        }
    }


    //Report the findings. 
    FUNCINFO("Attempting to claim a mutex");
    try{
        //File-based locking is used so this program can be run over many patients concurrently.
        //
        //Try open a named mutex. Probably created in /dev/shm/ if you need to clear it manually...
        boost::interprocess::named_mutex mutex(boost::interprocess::open_or_create,
                                               "dicomautomaton_operation_evaluatendvstats_mutex");
        boost::interprocess::scoped_lock<boost::interprocess::named_mutex> lock(mutex);

        if(OutFilename.empty()){
            OutFilename = Get_Unique_Sequential_Filename("/tmp/dicomautomaton_evaluatendvstats_", 6, ".csv");
        }
        const auto FirstWrite = !Does_File_Exist_And_Can_Be_Read(OutFilename);
        std::fstream FO_tcp(OutFilename, std::fstream::out | std::fstream::app);
        if(!FO_tcp){
            throw std::runtime_error("Unable to open file for reporting derivative data. Cannot continue.");
        }
        if(FirstWrite){ // Write a CSV header.
            FO_tcp << "UserComment,"
                   << "PatientID,"
                   << "ROIname,"
                   << "NormalizedROIname,"
                   << "HeterogeneityIndex,"
                   << "ConformityNumber,"
                   << "DoseMin,"
                   << "DoseMean,"
                   << "DoseMedian,"
                   << "DoseMax,"
                   << "DoseStdDev,"
                   << "VoxelCount"
                   << std::endl;
        }
        for(const auto &av : ud_PTV.accumulated_voxels){
            const auto lROIname = av.first;
            const auto DoseMin = Stats::Min( av.second );
            const auto DoseMean = Stats::Mean( av.second );
            const auto DoseMedian = Stats::Median( av.second );
            const auto DoseMax = Stats::Max( av.second );
            const auto DoseStdDev = std::sqrt(Stats::Unbiased_Var_Est( av.second ));
            const auto HeterogeneityIndex = HI[lROIname];
            const auto ConformityNumber = CN[lROIname];

            FO_tcp  << UserComment.value_or("") << ","
                    << patient_ID         << ","
                    << lROIname           << ","
                    << X(lROIname)        << ","
                    << HeterogeneityIndex << ","
                    << ConformityNumber   << ","
                    << DoseMin            << ","
                    << DoseMean           << ","
                    << DoseMedian         << ","
                    << DoseMax            << ","
                    << DoseStdDev         << ","
                    << av.second.size()
                    << std::endl;
        }
        FO_tcp.flush();
        FO_tcp.close();

    }catch(const std::exception &e){
        FUNCERR("Unable to write to output dose-volume stats file: '" << e.what() << "'");
    }

    return DICOM_data;
}
