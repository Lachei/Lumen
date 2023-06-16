#pragma once 
#include <inttypes.h> 
#include <string_view>
#include <vector>
#include <ranges>

inline auto s_range(const auto& v){return std::ranges::iota_view(size_t(0), v.size());}
inline auto i_range(auto v){return std::ranges::iota_view(decltype(v)(0), v);}

struct PointCloudData{
	std::vector<vec3> positions;
	std::vector<vec3> colors;
};

#pragma pack(push, 1)
struct PointDataRecordFormat0{
    int32_t  x;
    int32_t  y;
    int32_t  z;
    uint16_t intensity;
    uint8_t return_number:3;
    uint8_t number_of_returns:3;
    uint8_t scan_direction_flag:1;
    uint8_t edge_of_flight_line:1;
    uint8_t classification;
    char    scan_angle_rank;
    uint8_t user_data;
    uint16_t point_source_id;
};

struct PointDataRecordFormat1{
    PointDataRecordFormat0 format0;
    double gps_time;
};

struct rgb{ uint16_t red, green, blue;};

struct PointDataRecordFormat2{
    PointDataRecordFormat0 format0;
    rgb color;
};

struct PointDataRecordFormat3{
    PointDataRecordFormat0 format0;
    double gps_time;
    rgb    color;
};

struct WavePacket{    
    uint8_t wave_packet_descriptor_index;
    uint64_t byte_offset_to_waveform_data;
    uint32_t waveform_packet_size_in_bytes;
    float   return_point_waveform_location;
    float   x_t;
    float   y_t;
    float   z_t;
};

struct PointDataRecordFormat4{
    PointDataRecordFormat0 format0;
    double gps_time;
    WavePacket wave_packet;
};

struct PointDataRecordFormat5{
    PointDataRecordFormat3 format3;
    WavePacket wave_packet;
}; 
#pragma pack(pop)
// includes a header only importer for .lid data
// see https://www.asprs.org/wp-content/uploads/2010/12/LAS_1_4_r13.pdf for spec
struct LASFile{
    FILE* file_handle{};

    #pragma pack(push, 1)
    struct Header{
        char     signature[4]; // has to always be "LASF", is checked upon opening the file
        uint16_t file_src_id;
        uint16_t global_encoding;
        uint32_t guid_1;
        uint16_t guid_2;
        uint16_t guid_3;
        char     guid_4[8];
        uint8_t  version_maj;
        uint8_t  version_min;
        char     system_ident[32];
        char     generating_software[32];
        uint16_t creation_day;
        uint16_t creation_year;
        uint16_t header_size;
        uint32_t data_offset;
        uint32_t variable_length_amt;
        uint8_t  point_data_record_format;
        uint16_t point_data_record_length;
        uint32_t legacy_number_of_point_records;
        uint32_t legacy_number_of_points_by_return[5];
        double   x_scale;
        double   y_scale;
        double   z_scale;
        double   x_offset;
        double   y_offset;
        double   z_offset;
        double   max_x;
        double   min_x;
        double   max_y;
        double   min_y;
        double   max_z;
        double   min_z;
        uint64_t start_of_waveform_data_packet_record;
        uint64_t start_of_first_extended_variable_length_record;
        uint32_t number_of_extended_variable_length_records;
        uint64_t number_of_point_records;
        uint64_t number_of_points_by_return[15];
    } header;
    #pragma pack(pop)
   

    LASFile(std::string_view filename){
        file_handle = fopen(filename.data(), "rb");
        if(!file_handle){
            std::cout << "Could not open" << filename << std::endl;
            return;
        }

        // check signature and load header
        size_t read_size = fread(&header, sizeof(header), 1, file_handle);
        if(read_size != 1){
            std::cout << "Error reading header info for " << filename << std::endl;
            return;
        }
        if(header.signature[0] != 'L' || header.signature[1] != 'A' || header.signature[2] != 'S' || header.signature[3] != 'F'){
            std::cout << "Signature of file is not LASF (required for LIR file)" << std::endl;
            return;
        }
    }

    ~LASFile(){ if(file_handle) fclose(file_handle);}

    PointCloudData load_point_cloud_data(){
        PointCloudData ret{};
        
        if(!file_handle)
            return ret;

        fseek(file_handle, header.data_offset, SEEK_SET);
        switch(header.point_data_record_format){
        case 0:{
            break;
        }
        case 1: break;
        case 2: {
            size_t read_size = header.legacy_number_of_point_records ? header.legacy_number_of_point_records: header.point_data_record_length;
            read_size *= sizeof(PointDataRecordFormat2);
            std::vector<PointDataRecordFormat2> data(read_size / sizeof(PointDataRecordFormat2));
            size_t read_bytes = fread(data.data(), 1, read_size, file_handle);
            if(read_bytes != read_size){
                std::cout << "Error reading data" << std::endl;
                return ret;
            }
            // convert to return data
            ret.positions.resize(data.size());
            ret.colors.resize(data.size());
            for(size_t i: s_range(data)) {
                ret.positions[i] = {float(data[i].format0.x * header.x_scale + header.x_offset)
                                    ,float(data[i].format0.y * header.y_scale + header.y_offset)
                                    ,float(data[i].format0.z * header.z_scale + header.z_offset)};
                //assert(ret.positions[i].x >= header.min_x && ret.positions[i].x <= header.max_x &&
                //        ret.positions[i].y >= header.min_y && ret.positions[i].y <= header.max_y &&
                //        ret.positions[i].z >= header.min_z && ret.positions[i].z <= header.max_z);
                ret.positions[i] = {ret.positions[i].x, ret.positions[i].z, ret.positions[i].y};
                ret.colors[i] = {float(data[i].color.red / 0xffffp0), float(data[i].color.green / 0xffffp0), float(data[i].color.blue / 0xffffp0)};
            }
        }
        case 3: break;
        case 4: break;
        case 5: break;
        default: return ret;
        }
        return ret;
    }
};