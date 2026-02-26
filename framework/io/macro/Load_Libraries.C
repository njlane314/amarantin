{
    gSystem->AddIncludePath("-Iframework/io/include/");
    gSystem->AddDynamicPath("build/lib");
    gSystem->Load("build/lib/libIO.so");
}
