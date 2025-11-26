from huaweicloudsdkcore.exceptions import exceptions
from huaweicloudsdkcore.region.region import Region
from huaweicloudsdkiotda.v5 import ListDevicesRequest, IoTDAClient
from huaweicloudsdkcore.auth.credentials import BasicCredentials, DerivedCredentials

if __name__ == "__main__":
    # Hardcoded credentials for testing
    ak = "HPUA9PTFXFEA4DNUYWZP"
    sk = "YFhpfKN1CWrF5MYHah0qIahWJhlfBjTy5hDARXlt"
    project_id = "9e84cbf7c7c642059df96a94aa97661a"
    # region_id：如果是上海一，请填写"cn-east-3"；如果是北京四，请填写"cn-north-4"；如果是华南广州，请填写"cn-south-1"
    region_id = "cn-north-4"
    # endpoint：请在控制台的"总览"界面的"平台接入地址"中查看"应用侧"的https接入地址
    endpoint = "386ca38bcf.st1.iotda-app.cn-north-4.myhuaweicloud.com"

    # 标准版/企业版：需自行创建Region对象
    REGION = Region(region_id, endpoint)

    # 创建认证
    # 创建BasicCredentials实例并初始化
    credentials = BasicCredentials(ak, sk, project_id)

    # 标准版/企业版需要使用衍生算法，基础版请删除配置"with_derived_predicate"
    credentials.with_derived_predicate(DerivedCredentials.get_default_derived_predicate())

    # 基础版：请选择IoTDAClient中的Region对象 如： .with_region(IoTDARegion.CN_NORTH_4)
    # 标准版/企业版：需要使用自行创建的Region对象
    # 配置是否忽略SSL证书校验， 默认不忽略： .with_http_config(HttpConfig(ignore_ssl_verification=True)) \
    client = IoTDAClient.new_builder() \
        .with_credentials(credentials) \
        .with_region(REGION) \
        .build()

    try:
        # 实例化请求对象
        request = ListDevicesRequest()
        # 调用查询设备列表接口
        response = client.list_devices(request)
        print("=" * 60)
        print("                IoTDA 设备列表测试结果")
        print("=" * 60)
        print(response)
        print("=" * 60)
    except exceptions.ClientRequestException as e:
        print("=" * 60)
        print("                IoTDA API 错误信息")
        print("=" * 60)
        print(f"状态码: {e.status_code}")
        print(f"请求ID: {e.request_id}")
        print(f"错误代码: {e.error_code}")
        print(f"错误信息: {e.error_msg}")
        print("=" * 60)