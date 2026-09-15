#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace SaveSlot {

inline std::filesystem::path path(const std::filesystem::path& directory, size_t slot) {
  return directory / ("save_slot_"+std::to_string(slot)+".sav");
  }

inline bool hasAny(const std::filesystem::path& directory) {
  std::error_code error;
  std::filesystem::directory_iterator it(directory,error), end;
  for(; !error && it!=end; it.increment(error)) {
    const auto name=it->path().filename().u8string();
    std::u8string_view slot=name;
    if(!slot.starts_with(u8"save_slot_") || !slot.ends_with(u8".sav"))
      continue;
    slot.remove_prefix(10);
    slot.remove_suffix(4);
    if(slot.empty() || slot.find_first_not_of(u8"0123456789")!=slot.npos)
      continue;
    if(std::filesystem::is_regular_file(it->symlink_status(error)))
      return true;
    }
  return false;
  }

// Remove only a regular save file, never a directory or a symbolic link.
inline bool remove(const std::filesystem::path& directory, size_t slot, std::error_code& error) {
  error.clear();
  const auto file = path(directory,slot);
  const auto status = std::filesystem::symlink_status(file,error);
  if(error)
    return false;
  if(!std::filesystem::is_regular_file(status)) {
    error = std::make_error_code(std::errc::invalid_argument);
    return false;
    }
  const bool removed = std::filesystem::remove(file,error);
  if(!removed && !error)
    error = std::make_error_code(std::errc::no_such_file_or_directory);
  return removed && !error;
  }

}
