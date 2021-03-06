//OFF_Mesh_File_Loader.cc - A part of DICOMautomaton 2019. Written by hal clark.
//
// This program loads surface meshes from OFF files.
//

#include <cmath>
#include <cstdint>
#include <exception>
#include <fstream>
#include <list>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>    

#include <boost/filesystem.hpp>
#include <cstdlib>            //Needed for exit() calls.

#include "Structs.h"
#include "YgorMath.h"         //Needed for vec3 class.
#include "YgorMathIOOFF.h"
#include "YgorMisc.h"         //Needed for FUNCINFO, FUNCWARN, FUNCERR macros.
#include "YgorString.h"       //Needed for SplitStringToVector, Canonicalize_String2, SplitVector functions.


bool Load_Mesh_From_OFF_Files( Drover &DICOM_data,
                               std::map<std::string,std::string> & /* InvocationMetadata */,
                               const std::string &,
                               std::list<boost::filesystem::path> &Filenames ){

    //This routine will attempt to load OFF-format files as surface meshes. Note that not all OFF files contain meshes,
    // and support for OFF files is limited to a simplified subset. Note that a non-OFF file that is passed to this
    // routine will be fully parsed as an OFF file in order to assess validity. This can be problematic for multiple
    // reasons.
    //
    // Note: This routine returns false only iff a file is suspected of being suited for this loader, but could not be
    //       loaded (e.g., the file seems appropriate, but a parsing failure was encountered).
    //
    if(Filenames.empty()) return true;

    size_t i = 0;
    const size_t N = Filenames.size();

    auto bfit = Filenames.begin();
    while(bfit != Filenames.end()){
        FUNCINFO("Parsing file #" << i+1 << "/" << N << " = " << 100*(i+1)/N << "%");
        ++i;
        const auto Filename = bfit->string();

        DICOM_data.smesh_data.emplace_back( std::make_shared<Surface_Mesh>() );

        try{
            //////////////////////////////////////////////////////////////
            // Attempt to load the file.
            std::ifstream FI(Filename.c_str(), std::ios::in);
            if(!ReadFVSMeshFromOFF(DICOM_data.smesh_data.back()->meshes, FI)){
                throw std::runtime_error("Unable to read mesh from file.");
            }
            FI.close();
            //////////////////////////////////////////////////////////////

            // Reject the file if the mesh is not valid.
            const auto N_verts = DICOM_data.smesh_data.back()->meshes.vertices.size();
            const auto N_faces = DICOM_data.smesh_data.back()->meshes.faces.size();
            if( (N_verts == 0)
            ||  (N_faces == 0) ){
                throw std::runtime_error("Unable to read mesh from file.");
            }

            FUNCINFO("Loaded surface mesh with " 
                     << N_verts << " vertices and "
                     << N_faces << " faces");
            bfit = Filenames.erase( bfit ); 
            continue;
        }catch(const std::exception &e){
            FUNCINFO("Unable to load as OFF mesh file");
            DICOM_data.smesh_data.pop_back();
        };

        //Skip the file. It might be destined for some other loader.
        ++bfit;
    }

    return true;
}
