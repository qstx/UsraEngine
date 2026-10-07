#pragma once

#include <Vfs/VirtualFileSystem.hpp>
#include <Core/StringInterop.hpp>
#include <spdlog/sinks/base_sink.h>
#include <stdexcept>

namespace Ursa
{
class VfsLogSink final : public spdlog::sinks::base_sink<std::mutex>
{
public:
	VfsLogSink(std::shared_ptr<VirtualFileSystem> filesystem, VirtualPath path, Size maxBytes, Size maxFiles)
	    : filesystem(std::move(filesystem)), path(std::move(path)), maxBytes(maxBytes), maxFiles(maxFiles)
	{
		auto writable = this->filesystem->CheckWritable(this->path);
		Require(writable);
		Require(this->filesystem->Append(this->path, {}));
	}

protected:
	void sink_it_(const spdlog::details::log_msg& message) override
	{
		spdlog::memory_buf_t formatted;
		formatter_->format(message, formatted);
		auto size = filesystem->Size(path);
		if (!size)
		{
			throw std::runtime_error(ToStdString(size.error));
		}
		if (size.value > 0 && (size.value > maxBytes || formatted.size() > maxBytes - size.value))
		{
			for (Size index = maxFiles; index > 0; --index)
			{
				auto target = Rotated(index);
				auto source = index == 1 ? path : Rotated(index - 1);
				Require(filesystem->Remove(target));
				auto exists = filesystem->Exists(source);
				if (!exists)
				{
					throw std::runtime_error(ToStdString(exists.error));
				}
				if (exists.value)
				{
					Require(filesystem->Rename(source, target));
				}
			}
		}
		const auto* bytes = reinterpret_cast<const UInt8*>(formatted.data());
		Require(filesystem->Append(path, Vector<UInt8>(bytes, bytes + formatted.size())));
	}

	void flush_() override
	{
	}

private:
	static void Require(const Result<void>& result)
	{
		if (!result)
		{
			throw std::runtime_error(ToStdString(result.error));
		}
	}

	VirtualPath Rotated(Size index) const
	{
		const auto dot = path.relative.rfind('.');
		const String suffix = "." + ToUrsaString(std::to_string(index));
		auto relative = dot == String::npos ? path.relative + suffix
		                                    : path.relative.substr(0, dot) + suffix + path.relative.substr(dot);
		return VirtualPath::Parse(path.scheme + ":" + relative).value;
	}

	std::shared_ptr<VirtualFileSystem> filesystem;
	VirtualPath path;
	Size maxBytes;
	Size maxFiles;
};
} // namespace Ursa
