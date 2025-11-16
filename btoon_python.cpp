#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <btoon/btoon.h>

namespace py = pybind11;

// Convert Python object to BTOON Value
btoon::Value py_to_value(const py::object& obj) {
    if (obj.is_none()) {
        return btoon::Value(nullptr);
    } else if (py::isinstance<py::bool_>(obj)) {
        return btoon::Value(obj.cast<bool>());
    } else if (py::isinstance<py::int_>(obj)) {
        // Python integers are arbitrary precision, but we need to fit them into int64/uint64
        try {
            // Try to fit as int64 first (covers negative and positive values up to 2^63-1)
            int64_t val = obj.cast<int64_t>();
            return btoon::Value(val);
        } catch (const py::cast_error&) {
            // If that fails, try uint64 for large positive numbers
            uint64_t val = obj.cast<uint64_t>();
            return btoon::Value(val);
        }
    } else if (py::isinstance<py::float_>(obj)) {
        return btoon::Value(obj.cast<double>());
    } else if (py::isinstance<py::str>(obj)) {
        return btoon::Value(obj.cast<std::string>());
    } else if (py::isinstance<py::bytes>(obj)) {
        std::string bytes_str = obj.cast<std::string>();
        return btoon::Value(std::vector<uint8_t>(bytes_str.begin(), bytes_str.end()));
    } else if (py::isinstance<py::list>(obj)) {
        std::vector<btoon::Value> arr;
        for (auto item : obj.cast<py::list>()) {
            arr.push_back(py_to_value(py::reinterpret_borrow<py::object>(item)));
        }
        return btoon::Value(arr);
    } else if (py::isinstance<py::dict>(obj)) {
        // Check for special extension dict format: {"type": int, "data": bytes}
        py::dict dict = obj.cast<py::dict>();
        if (dict.contains("type") && dict.contains("data") && dict.size() == 2) {
            try {
                int8_t ext_type = dict["type"].cast<int8_t>();
                std::string data_str = dict["data"].cast<std::string>();
                std::vector<uint8_t> data_vec(data_str.begin(), data_str.end());
                return btoon::Value(btoon::Extension{ext_type, data_vec});
            } catch (...) {
                // Not a valid extension, treat as regular dict
            }
        }

        // Regular dictionary
        std::map<std::string, btoon::Value> map;
        for (auto item : dict) {
            std::string key = py::str(item.first).cast<std::string>();
            map[key] = py_to_value(py::reinterpret_borrow<py::object>(item.second));
        }
        return btoon::Value(map);
    }

    throw std::runtime_error("Unsupported Python type");
}

// Convert BTOON Value to Python object
py::object value_to_py(const btoon::Value& value) {
    if (std::holds_alternative<std::nullptr_t>(value)) {
        return py::none();
    } else if (std::holds_alternative<bool>(value)) {
        return py::bool_(std::get<bool>(value));
    } else if (std::holds_alternative<int64_t>(value)) {
        return py::int_(std::get<int64_t>(value));
    } else if (std::holds_alternative<uint64_t>(value)) {
        return py::int_(std::get<uint64_t>(value));
    } else if (std::holds_alternative<double>(value)) {
        return py::float_(std::get<double>(value));
    } else if (std::holds_alternative<std::string>(value)) {
        return py::str(std::get<std::string>(value));
    } else if (std::holds_alternative<std::vector<uint8_t>>(value)) {
        const auto& bin = std::get<std::vector<uint8_t>>(value);
        return py::bytes(std::string(bin.begin(), bin.end()));
    } else if (std::holds_alternative<std::vector<btoon::Value>>(value)) {
        py::list lst;
        for (const auto& item : std::get<std::vector<btoon::Value>>(value)) {
            lst.append(value_to_py(item));
        }
        return lst;
    } else if (std::holds_alternative<std::map<std::string, btoon::Value>>(value)) {
        py::dict dict;
        for (const auto& [k, v] : std::get<std::map<std::string, btoon::Value>>(value)) {
            dict[py::str(k)] = value_to_py(v);
        }
        return dict;
    } else if (std::holds_alternative<btoon::Extension>(value)) {
        const auto& ext = std::get<btoon::Extension>(value);
        py::dict ext_dict;
        ext_dict["type"] = py::int_(ext.type);
        ext_dict["data"] = py::bytes(std::string(ext.data.begin(), ext.data.end()));
        return ext_dict;
    } else if (std::holds_alternative<btoon::Timestamp>(value)) {
        const auto& ts = std::get<btoon::Timestamp>(value);
        py::dict ts_dict;
        ts_dict["_btoon_type"] = py::str("Timestamp");
        ts_dict["seconds"] = py::int_(ts.seconds);
        return ts_dict;
    } else if (std::holds_alternative<btoon::Date>(value)) {
        const auto& date = std::get<btoon::Date>(value);
        py::dict date_dict;
        date_dict["_btoon_type"] = py::str("Date");
        date_dict["milliseconds"] = py::int_(date.milliseconds);
        return date_dict;
    } else if (std::holds_alternative<btoon::DateTime>(value)) {
        const auto& dt = std::get<btoon::DateTime>(value);
        py::dict dt_dict;
        dt_dict["_btoon_type"] = py::str("DateTime");
        dt_dict["nanoseconds"] = py::int_(dt.nanoseconds);
        return dt_dict;
    } else if (std::holds_alternative<btoon::BigInt>(value)) {
        const auto& bigint = std::get<btoon::BigInt>(value);
        py::dict bi_dict;
        bi_dict["_btoon_type"] = py::str("BigInt");
        bi_dict["bytes"] = py::bytes(std::string(bigint.bytes.begin(), bigint.bytes.end()));
        return bi_dict;
    } else if (std::holds_alternative<btoon::VectorFloat>(value)) {
        const auto& vec = std::get<btoon::VectorFloat>(value);
        py::list lst;
        for (float f : vec.data) {
            lst.append(py::float_(f));
        }
        return lst;
    } else if (std::holds_alternative<btoon::VectorDouble>(value)) {
        const auto& vec = std::get<btoon::VectorDouble>(value);
        py::list lst;
        for (double d : vec.data) {
            lst.append(py::float_(d));
        }
        return lst;
    }

    return py::none();
}

// Python-friendly encode function
py::bytes encode_py(const py::object& obj, bool compress = false, bool auto_tabular = true) {
    try {
        btoon::Value value = py_to_value(obj);
        btoon::EncodeOptions opts;
        opts.compress = compress;
        opts.auto_tabular = auto_tabular;

        auto encoded = btoon::encode(value, opts);
        return py::bytes(std::string(encoded.begin(), encoded.end()));
    } catch (const btoon::BtoonException& e) {
        throw py::value_error(std::string("BTOON encoding error: ") + e.what());
    } catch (const std::exception& e) {
        throw py::value_error(std::string("Encoding error: ") + e.what());
    }
}

// Python-friendly decode function
py::object decode_py(const py::bytes& data, bool decompress = false) {
    try {
        std::string data_str = data.cast<std::string>();
        std::vector<uint8_t> data_vec(data_str.begin(), data_str.end());

        btoon::DecodeOptions opts;
        opts.auto_decompress = decompress;

        auto value = btoon::decode(data_vec, opts);
        return value_to_py(value);
    } catch (const btoon::BtoonException& e) {
        throw py::value_error(std::string("BTOON decoding error: ") + e.what());
    } catch (const std::exception& e) {
        throw py::value_error(std::string("Decoding error: ") + e.what());
    }
}

PYBIND11_MODULE(_btoon, m) {
    m.doc() = "BTOON: Binary TOON serialization format - native C++ extension";

    m.def("encode", &encode_py,
          py::arg("obj"),
          py::arg("compress") = false,
          py::arg("auto_tabular") = true,
          "Encode a Python object to BTOON binary format.\n\n"
          "Args:\n"
          "    obj: Python object to encode (dict, list, str, int, float, bool, None, bytes)\n"
          "    compress: Whether to compress the output (default: False)\n"
          "    auto_tabular: Automatically detect and use tabular encoding (default: True)\n\n"
          "Returns:\n"
          "    bytes: BTOON encoded data\n\n"
          "Raises:\n"
          "    ValueError: If encoding fails");

    m.def("decode", &decode_py,
          py::arg("data"),
          py::arg("decompress") = false,
          "Decode BTOON binary data to a Python object.\n\n"
          "Args:\n"
          "    data: BTOON encoded bytes\n"
          "    decompress: Whether to automatically decompress (default: False)\n\n"
          "Returns:\n"
          "    Python object (dict, list, str, int, float, bool, None, or bytes)\n\n"
          "Raises:\n"
          "    ValueError: If decoding fails");

    m.def("version", &btoon::version, "Get BTOON version string");

    m.attr("__version__") = btoon::version();
}
