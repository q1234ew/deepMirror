# Patch only a generated copy; the upstream checkout remains unchanged.
file(READ "${SOURCE}/Windows-SDK/Insight9SDK.cpp" sdk)
function(replace_checked old new)
 string(FIND "${sdk}" "${old}" position)
 if(position EQUAL -1)
  message(FATAL_ERROR "Pinned SDK patch anchor not found: ${old}")
 endif()
 string(REPLACE "${old}" "${new}" sdk "${sdk}")
 set(sdk "${sdk}" PARENT_SCOPE)
endfunction()
# Upstream caches by logical camera id (0..2), so reserve all three entries.
replace_checked("camera_params cachedInitialParams[UVC_NUM]" "camera_params cachedInitialParams[LOGICAL_CAM_NUM]")
replace_checked("bool hasCachedInitialParams[UVC_NUM]" "bool hasCachedInitialParams[LOGICAL_CAM_NUM]")
if(SPLIT_USB)
 replace_checked("#define PRODUCT_ID 0x0b5c" "#define PRODUCT_ID 0x0104")
 # This device has RGB MI_02, gray MI_04, depth MI_06. Only RGB/depth needed.
 replace_checked("if (upper.find(\"MI_03\") != std::string::npos) {" "if (upper.find(\"MI_02\") != std::string::npos) {")
 replace_checked("upper.find(\"MI_00\") != std::string::npos" "upper.find(\"MI_06\") != std::string::npos")
 replace_checked("upper.find(\"MI_04\") != std::string::npos" "upper.find(\"MI_02\") != std::string::npos")
 # Use slot 1 for the standalone depth endpoint. No grayscale stream 1 exists.
 replace_checked("if (!configureStream(1, fmt1, width1, height1, fps1)) {" "if (false && !configureStream(1, fmt1, width1, height1, fps1)) {")
 # Preserve independent native depth dimensions instead of forcing gray size.
 replace_checked("if (depthW != grayW || depthH != grayH) {" "if (false && (depthW != grayW || depthH != grayH)) {")
endif()
file(MAKE_DIRECTORY "${OUTPUT}")
file(WRITE "${OUTPUT}/Insight9SDK.cpp" "${sdk}")
