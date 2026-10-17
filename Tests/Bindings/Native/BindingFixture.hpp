#pragma once

#include <Scripting/Bindings/BindingRuntime.hpp>
#include <Scripting/Bindings/BindingMath.hpp>

namespace Ursa::Tests::BindingFixture
{
namespace B = Scripting::Bindings;
using Point = B::Wire::Value_BindingFixture_Point;
using OptionalPoint = B::Wire::Value_BindingFixture_Point_;
using Packet = B::Wire::Value_BindingFixture_Packet;

inline Result<B::Wire::Value_BindingFixture_MathPacket> EchoMath(B::BindingCall&,
                                                                 const B::Wire::Value_BindingFixture_MathPacket& value)
{
	auto output = value;
	output.F_Position = B::WriteVector3<B::Wire::Value_Ursa_Vector3>(B::ReadVector3(value.F_Position));
	return Result<B::Wire::Value_BindingFixture_MathPacket>::Success(output);
}

inline Result<B::Wire::Value_Ursa_Matrix4x4>
MultiplyMatrices(B::BindingCall&, const B::Wire::Value_Ursa_Matrix4x4& left, const B::Wire::Value_Ursa_Matrix4x4& right)
{
	return Result<B::Wire::Value_Ursa_Matrix4x4>::Success(
	    B::WriteMatrix<B::Wire::Value_Ursa_Matrix4x4>(B::ReadMatrix(left) * B::ReadMatrix(right)));
}

inline Result<B::Wire::Value_Ursa_Quaternion> EchoQuaternion(B::BindingCall&,
                                                             const B::Wire::Value_Ursa_Quaternion& value)
{
	return Result<B::Wire::Value_Ursa_Quaternion>::Success(
	    B::WriteQuaternion<B::Wire::Value_Ursa_Quaternion>(B::ReadQuaternion(value)));
}

inline Result<B::Wire::Value_Ursa_Color> EchoColor(B::BindingCall&, const B::Wire::Value_Ursa_Color& value)
{
	return Result<B::Wire::Value_Ursa_Color>::Success(B::WriteColor<B::Wire::Value_Ursa_Color>(B::ReadColor(value)));
}

inline Result<B::Wire::Value_BindingFixture_Scalars> EchoScalars(B::BindingCall&,
                                                                 const B::Wire::Value_BindingFixture_Scalars& value)
{
	return Result<B::Wire::Value_BindingFixture_Scalars>::Success(value);
}

inline Result<UInt32> ReadObject(B::BindingCall&, const B::Wire::Value_BindingFixture_FixtureHandle& handle)
{
	return Result<UInt32>::Success(handle.F_Index);
}

inline Result<Int64> Add(B::BindingCall&, Int64 left, Int64 right)
{
	if ((right > 0 && left > std::numeric_limits<Int64>::max() - right) ||
	    (right < 0 && left < std::numeric_limits<Int64>::min() - right))
	{
		return Result<Int64>::Failure("Addition overflow");
	}
	return Result<Int64>::Success(left + right);
}

inline Result<Point> EchoPoint(B::BindingCall&, const Point& value)
{
	return Result<Point>::Success(value);
}

inline Result<B::Utf8View> EchoText(B::BindingCall& call, const B::Utf8View& value)
{
	return Result<B::Utf8View>::Success(value.present ? call.arena.StringValue(B::ReadString(value)) : B::Utf8View{});
}

inline Result<B::DataView> EchoValues(B::BindingCall& call, const B::DataView& values)
{
	return Result<B::DataView>::Success(call.arena.Array(static_cast<const Point*>(values.data), Size(values.length)));
}

inline Result<OptionalPoint> EchoOptional(B::BindingCall&, const OptionalPoint& value)
{
	return Result<OptionalPoint>::Success(value);
}

inline Result<Packet> EchoPacket(B::BindingCall& call, const Packet& value)
{
	Packet output = value;
	output.F_Name = B::TakeBindingResult(EchoText(call, value.F_Name));
	output.F_Values = B::TakeBindingResult(EchoValues(call, value.F_Values));
	return Result<Packet>::Success(output);
}

inline Result<Int64> Sum(B::BindingCall&, const B::DataView& values)
{
	Int64 result = 0;
	const auto* input = static_cast<const Int32*>(values.data);
	for (Size index = 0; index < values.length; ++index)
	{
		result += input[index];
	}
	return Result<Int64>::Success(result);
}

class Context final : public B::BindingContext
{
public:
	UInt32 modifications = 0;
	UInt64 budget = B::maximumBytes;
	std::vector<Int32> captured;

	UInt64 SessionIdentity() const override
	{
		return 17;
	}

	void CheckHandle(const void* data, Size size, UInt32 kind) const override
	{
		if (size != sizeof(B::Wire::Value_BindingFixture_FixtureHandle))
		{
			throw std::runtime_error("Fixture handle layout differs");
		}
		const auto& value = *static_cast<const B::Wire::Value_BindingFixture_FixtureHandle*>(data);
		if (value.F_Session != 17 || value.F_Index != 1 || value.F_Generation != 3 || value.F_Kind != kind ||
		    value.F_Reserved != 0)
		{
			throw std::runtime_error("Stale fixture handle or wrong kind");
		}
	}

	UInt64 TransferBudget() const override
	{
		return budget;
	}
};

inline Result<void> CaptureInput(B::BindingCall& call, const B::DataView& values)
{
	auto& captured = static_cast<Context&>(call.context).captured;
	if (!values.length)
	{
		captured.clear();
	}
	else
	{
		const auto* data = static_cast<const Int32*>(values.data);
		captured.assign(data, data + values.length);
	}
	return {};
}

inline Result<B::DataView> ReadCaptured(B::BindingCall& call)
{
	const auto& captured = static_cast<Context&>(call.context).captured;
	return Result<B::DataView>::Success(call.arena.Array(captured.data(), captured.size()));
}

inline Result<B::Utf8View> Mutate(B::BindingCall& call, const B::Utf8View& name)
{
	auto result = call.arena.StringValue(B::ReadString(name));
	++static_cast<Context&>(call.context).modifications;
	return Result<B::Utf8View>::Success(result);
}

inline Result<UInt32> GetCount(B::BindingCall& call)
{
	return Result<UInt32>::Success(static_cast<Context&>(call.context).modifications);
}

inline Result<void> Fail(B::BindingCall& call, Int32 code)
{
	call.arena.Allocate(128, 1);
	throw std::runtime_error("Fixture failure " + std::to_string(code));
}

inline Result<B::Utf8View> InvalidOutput(B::BindingCall& call)
{
	auto* data = static_cast<UInt8*>(call.arena.Allocate(1, 1));
	*data = 0xff;
	return Result<B::Utf8View>::Success({data, 1, 1, 0});
}

inline Result<B::Utf8View> BorrowedOutput(B::BindingCall&, const B::Utf8View& value)
{
	return Result<B::Utf8View>::Success(value);
}
} // namespace Ursa::Tests::BindingFixture
