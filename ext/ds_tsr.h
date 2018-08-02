#pragma once

#include "ext/messages.h"
#include "ext/ds_tsr_args.h"
#include "utils/utils.h"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <iterator>
#include <iostream>

namespace fs = boost::filesystem;

namespace ORB_SLAM2 {
    namespace ext {
        //read tsr info from txt file
        struct tsr_data {
            std::string _image_name;
            std::string _time_point;

            std::vector<std::tuple<int, double, std::vector<double> > > _vec_tsr_info;
            std::vector< std::vector<int> > _vec_cov;
            std::vector<double> _vec_pos;

            void get_next_item(std::string linestr)
            {
                std::stringstream ss;
                ss << linestr;

                boost::property_tree::ptree pt;
                _vec_tsr_info.clear();
                _vec_cov.clear();
                _vec_pos.clear();

                boost::property_tree::read_json(ss, pt);
                _image_name = pt.get<std::string>("image");
                _time_point = pt.get<std::string>("time_point");
                if (pt.get_optional<std::string>("tsr_info"))
                {
                    for (auto& node : pt.get_child("tsr_info")) {
                        int _classid;
                        double _confidence;
                        _classid = node.second.get<int>("class_id");
                        _confidence = node.second.get<double>("confidence");
                        std::vector<double> box;
                        for (auto &temp : node.second.get_child("rectangle")) {
                            box.push_back(temp.second.get_value < double >());
                        }
                        _vec_tsr_info.push_back(std::make_tuple(_classid, _confidence, box));
                    }
                }
                if (pt.get_optional<std::string>("pos_info")) {
                    int rows = 0;
                    for (auto& node : pt.get_child("pos_info.cov")) {
                        std::vector<int> cov;
                        for (auto &cell : node.second) {
                            cov.push_back(cell.second.get_value<int>());
                        }
                        _vec_cov.push_back(cov);
                    }
                    for(auto& node : pt.get_child("pos_info.pos")) {
                        _vec_pos.push_back(node.second.get_value<double>());
                    }
                }
            }
        };

        class ds_tsr : public boost::iterator_facade<
                            ds_tsr,
                            const std::tuple<time_point_t, image_t, tsr_info_opt_t, pos_info_opt_t>,
                            boost::single_pass_traversal_tag > 
        {
            std::vector<fs::path> _image_files;
            std::fstream _jsonfile;
            std::int64_t _image_index{-1};
            std::tuple<time_point_t, image_t, tsr_info_opt_t, pos_info_opt_t> _item;
            int img_width{ 1280 };
            int img_height{ 720 };
            utils::gps_info _org_gps;
            ds_tsr_args _ds_args;

            void read_image_files(fs::path const& path_to_image_folder_)
            {
                if (false == path_to_image_folder_.empty())
                {
                    if (fs::exists(path_to_image_folder_) && fs::is_directory(path_to_image_folder_))
                    {
                        std::copy(fs::directory_iterator(path_to_image_folder_), fs::directory_iterator(),
                            std::back_inserter(_image_files));
                        if (_image_files.size())
                        {
                            std::sort(_image_files.begin(), _image_files.end());

                            //read first image and set image width and height
                            image_t image = cv::imread(_image_files[0].generic_string(), CV_LOAD_IMAGE_UNCHANGED);
                            img_width = image.cols;
                            img_height = image.rows;
                        }
                    }
                    else
                        throw std::runtime_error(path_to_image_folder_.generic_string() + " does not exist or it is not a directory");

                }
            }

            void read_jsonparser(fs::path const& jsonFilename)
            {
                if (false == jsonFilename.empty())
                {
                    _jsonfile.open(jsonFilename.string(), std::fstream::in);
                    if (false == _jsonfile.is_open())
                        throw std::runtime_error("Unable to open JSON file " + jsonFilename.string());
                }
            }

            int extract_num(std::string name)
            {
                int extract_value = -1;
                auto start_index = name.rfind("/");
                auto end_index = name.rfind(".");
                if ((start_index != std::string::npos) && (end_index != std::string::npos))
                {
                    start_index += 1;
                    extract_value = boost::lexical_cast<int>(name.substr(start_index, end_index - start_index));
                }
                return extract_value;
            }
        public:
            void get_next_item()
            {
                if (_image_index < _image_files.size())
                {
                    int cur_image_number = extract_num(_image_files[_image_index].generic_string());
                    if (cur_image_number != -1)
                    {
                        image_t image = cv::imread(_image_files[_image_index].generic_string(), CV_LOAD_IMAGE_UNCHANGED);
                        time_point_t timestamp = 0;
                        tsr_info_opt_t tsr_ds;
                        pos_info_opt_t gps_ds;
                        std::tie(timestamp,std::ignore, tsr_ds, gps_ds) = get_next_tsr();
                        _item = std::make_tuple(timestamp, image, tsr_ds, gps_ds);
                    }
                    _image_index++;
                }
                else
                    _image_index = -1;
            }

            slam_input_t get_next_tsr()
            {
                std::string line;
                if (std::getline(_jsonfile, line))
                {
                    tsr_data tsr_data_obj;
                    tsr_data_obj.get_next_item(line);
                    //tsr_data_obj.print_data();
                    tsr_info_opt_t cur_tsr_info;
                    std::vector<ORB_SLAM2::ext::traffic_sign> traffic_signs;
                    if (tsr_data_obj._vec_tsr_info.size())
                    {
                        for (auto item : tsr_data_obj._vec_tsr_info)
                        {
                            ORB_SLAM2::ext::traffic_sign t;
                            std::vector<double> rect;
                            std::tie(t.class_id, t.confidence, rect) = item;
                            transform_rect(rect, t.box);
                            traffic_signs.push_back(t);
                        }
                        cur_tsr_info = traffic_signs;
                    }
                    pos_info_opt_t cur_gps_ds;
                    if (tsr_data_obj._vec_pos.size())
                    {
                        utils::gps_info gps;
                        gps._lat = tsr_data_obj._vec_pos[0];
                        gps._lon = tsr_data_obj._vec_pos[1];
                        gps._alt = tsr_data_obj._vec_pos[2];
                        
                        if (!_image_index)
                            _org_gps = gps;
                        cur_gps_ds = utils::get_pos_info(_org_gps, gps);
                    }
                    return std::make_tuple(stod(tsr_data_obj._time_point), cv::Mat(), cur_tsr_info, cur_gps_ds);
                }
                return std::make_tuple(0, cv::Mat(), boost::none, boost::none);
            }

            ds_tsr(ds_tsr_args ds_args_) 
                : _ds_args(std::move(ds_args_))
            {
                read_image_files(_ds_args.images());
                _image_index = 0;
            }

            ds_tsr(const ds_tsr& obj) :_ds_args(obj._ds_args)
            {
                if (obj._image_index != -1)
                {
                    _image_index = 0;
                    copy(obj._image_files.begin(), obj._image_files.end(), back_inserter(_image_files));
                    read_jsonparser(_ds_args.tsr_info());
                    get_next_item();
                }
            }
            ds_tsr()
            {
            }
            ~ds_tsr()
            {
                _jsonfile.close();
            }
            void transform_rect(std::vector<double> &rect_arr, cv::Rect& box, bool is_absolute = false)
            {
                const int min_roi_width = 90;
                const int min_roi_height = 90;

                if (true == is_absolute) {
                    box.x = rect_arr[0];
                    box.y = rect_arr[1];
                    box.width = rect_arr[2] - rect_arr[0];
                    box.height = rect_arr[3] - rect_arr[1];
                }
                else {
                    int ymin = int(rect_arr[0] * img_height);
                    int xmin = int(rect_arr[1] * img_width);
                    int ymax = int(rect_arr[2] * img_height);
                    int xmax = int(rect_arr[3] * img_width);
                    box.x = xmin;
                    box.y = ymin;
                    box.width = xmax - xmin;
                    box.height = ymax - ymin;
                }
            }

            bool equal(const ds_tsr& other) const { return _image_index == other._image_index; }
            void increment() { get_next_item(); }
            auto& dereference() const { return _item; }

        };
        
    }
}