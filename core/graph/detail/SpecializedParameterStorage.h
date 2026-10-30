#pragma once
class CKParameter;
namespace vdev {
struct ParameterValue;
struct ParameterValueDescription;
bool DescribeSpecializedParameter(CKParameter *, ParameterValueDescription &);
bool WriteSpecializedParameter(CKParameter *, const ParameterValue &);
}
