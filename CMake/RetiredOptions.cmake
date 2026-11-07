if(DEFINED URSA_ENABLE_DOTNET)
  message(FATAL_ERROR
    "URSA_ENABLE_DOTNET has been removed. C# is always available. Remove the -D argument "
    "and clear the old cache entry with cmake -UURSA_ENABLE_DOTNET -S <engine> -B <build>. "
    "Keep your existing generator and other settings.")
endif()
