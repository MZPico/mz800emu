
If the application is running in Linux or in the MSYS2 console, libcurl works without problems.
However, if the application is running in Windows separately from an environment with copied runtime libraries, you need to create a ./certs directory and copy the cacert.pem file into it

Download the file from https://curl.se/ca/cacert.pem
