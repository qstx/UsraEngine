#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <comdef.h>
#include <wrl/client.h>
#include <Core/Types.hpp>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace
{
using namespace Ursa;
using Microsoft::WRL::ComPtr;

_variant_t Invoke(IDispatch* object, const wchar_t* name, WORD flags, const std::vector<_variant_t>& arguments = {})
{
	if (!object)
	{
		_com_issue_error(E_POINTER);
	}
	DISPID id = 0;
	LPOLESTR member = const_cast<LPOLESTR>(name);
	HRESULT status = object->GetIDsOfNames(IID_NULL, &member, 1, LOCALE_USER_DEFAULT, &id);
	if (FAILED(status))
	{
		_com_issue_error(status);
	}
	std::vector<VARIANTARG> reversed;
	for (auto argument = arguments.rbegin(); argument != arguments.rend(); ++argument)
	{
		reversed.push_back(static_cast<const VARIANT&>(*argument));
	}
	DISPID put = DISPID_PROPERTYPUT;
	DISPPARAMS parameters{};
	parameters.rgvarg = reversed.data();
	parameters.cArgs = static_cast<UINT>(reversed.size());
	if (flags & DISPATCH_PROPERTYPUT)
	{
		parameters.rgdispidNamedArgs = &put;
		parameters.cNamedArgs = 1;
	}
	_variant_t result;
	status = object->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, flags, &parameters, &result, nullptr, nullptr);
	if (FAILED(status))
	{
		_com_issue_error(status);
	}
	return result;
}

ComPtr<IDispatch> FindSolution(const std::filesystem::path& solution)
{
	ComPtr<IRunningObjectTable> objects;
	if (FAILED(GetRunningObjectTable(0, &objects)))
	{
		return {};
	}
	ComPtr<IEnumMoniker> entries;
	ComPtr<IBindCtx> context;
	if (FAILED(objects->EnumRunning(&entries)) || FAILED(CreateBindCtx(0, &context)))
	{
		return {};
	}
	ComPtr<IMoniker> moniker;
	while (entries->Next(1, &moniker, nullptr) == S_OK)
	{
		LPOLESTR display = nullptr;
		const HRESULT named = moniker->GetDisplayName(context.Get(), nullptr, &display);
		const bool visualStudio = SUCCEEDED(named) && display && std::wstring(display).find(L"!VisualStudio.DTE.") == 0;
		CoTaskMemFree(display);
		if (visualStudio)
		{
			ComPtr<IUnknown> unknown;
			ComPtr<IDispatch> dte;
			if (SUCCEEDED(objects->GetObject(moniker.Get(), &unknown)) && SUCCEEDED(unknown.As(&dte)))
			{
				try
				{
					auto current = Invoke(dte.Get(), L"Solution", DISPATCH_PROPERTYGET);
					auto name = Invoke(current.pdispVal, L"FullName", DISPATCH_PROPERTYGET);
					if (name.vt == VT_BSTR && name.bstrVal && _wcsicmp(name.bstrVal, solution.c_str()) == 0)
					{
						return dte;
					}
				}
				catch (const _com_error&)
				{
					// Busy or unrelated instances are never selected as a fallback.
				}
			}
		}
		moniker.Reset();
	}
	return {};
}

void Navigate(IDispatch* dte, const wchar_t* file, Int32 line, Int32 column)
{
	auto operations = Invoke(dte, L"ItemOperations", DISPATCH_PROPERTYGET);
	auto window = Invoke(operations.pdispVal, L"OpenFile", DISPATCH_METHOD, {_variant_t(file), _variant_t(L"")});
	Invoke(window.pdispVal, L"Activate", DISPATCH_METHOD);
	auto document = Invoke(dte, L"ActiveDocument", DISPATCH_PROPERTYGET);
	auto selection = Invoke(document.pdispVal, L"Selection", DISPATCH_PROPERTYGET);
	Invoke(selection.pdispVal, L"MoveToLineAndOffset", DISPATCH_METHOD,
	       {_variant_t(static_cast<long>(line)), _variant_t(static_cast<long>(column)), _variant_t(false)});
	auto point = Invoke(selection.pdispVal, L"ActivePoint", DISPATCH_PROPERTYGET);
	const auto actualLine = Invoke(point.pdispVal, L"Line", DISPATCH_PROPERTYGET);
	const auto actualColumn = Invoke(point.pdispVal, L"LineCharOffset", DISPATCH_PROPERTYGET);
	if (static_cast<long>(actualLine) != line || static_cast<long>(actualColumn) != column)
	{
		_com_issue_error(E_INVALIDARG);
	}
	auto mainWindow = Invoke(dte, L"MainWindow", DISPATCH_PROPERTYGET);
	Invoke(mainWindow.pdispVal, L"Visible", DISPATCH_PROPERTYPUT, {_variant_t(true)});
	Invoke(mainWindow.pdispVal, L"Activate", DISPATCH_METHOD);
	std::wcout << L"Navigated " << file << L":" << line << L":" << column << L"\n";
}
} // namespace

int wmain(int argumentCount, wchar_t** arguments)
{
	if (argumentCount != 6)
	{
		std::cerr << "Usage: UrsaIdeBridge <devenv.exe> <solution.sln> <source.cs> <line> <column>\n";
		return 2;
	}
	const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	if (FAILED(initialized))
	{
		return 3;
	}
	Int32 exitCode = 1;
	try
	{
		const auto solution = std::filesystem::canonical(arguments[2]);
		const auto executable = std::filesystem::canonical(arguments[1]);
		const auto source = std::filesystem::canonical(arguments[3]);
		const Int32 line = std::stoi(arguments[4]);
		const Int32 column = std::stoi(arguments[5]);
		if (line < 1 || column < 1 || source.extension() != L".cs" || solution.extension() != L".sln" ||
		    executable.filename() != L"devenv.exe")
		{
			throw std::runtime_error("Invalid Visual Studio navigation request");
		}
		auto dte = FindSolution(solution);
		if (!dte)
		{
			// Windows file names cannot contain quotes; both arguments end in a file extension.
			std::wstring command = L"\"" + executable.wstring() + L"\" \"" + solution.wstring() + L"\"";
			STARTUPINFOW startup{};
			startup.cb = sizeof(startup);
			PROCESS_INFORMATION process{};
			if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
			                    solution.parent_path().c_str(), &startup, &process))
			{
				throw std::runtime_error("Cannot start Visual Studio");
			}
			CloseHandle(process.hThread);
			CloseHandle(process.hProcess);
		}
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(25);
		while (std::chrono::steady_clock::now() < deadline)
		{
			if (dte)
			{
				try
				{
					Navigate(dte.Get(), source.c_str(), line, column);
					exitCode = 0;
					break;
				}
				catch (const _com_error&)
				{
				}
			}
			Sleep(200);
			dte = FindSolution(solution);
		}
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
	}
	catch (const _com_error&)
	{
		std::cerr << "Visual Studio automation failed\n";
	}
	CoUninitialize();
	return exitCode;
}
