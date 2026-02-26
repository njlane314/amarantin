#include "TString.h"
#include "TSystem.h"

{
    TString include_flag = "-Iframework/io/include";
    TString include_path = gSystem->GetIncludePath();
    if (!include_path.Contains(include_flag))
        gSystem->AddIncludePath(include_flag);
}
