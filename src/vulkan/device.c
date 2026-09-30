#include "vulkan/device.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

// graphvex R3 — vulkan/device.c

struct Device {
    VkInstance instance;
    VkPhysicalDevice physical;
    VkDevice device;
    VkQueue queue;
    uint32_t queueFamily;
    char deviceName[256];
    char error[256];
};

static void set_err(Device *c, const char *msg) {
    snprintf(c->error, sizeof c->error, "%s", msg);
}

Device *Device_create(bool enableValidation) {
    (void)enableValidation;   // validation layers land in a debug slice
    Device *c = calloc(1, sizeof *c);
    if (!c) return NULL;
    set_err(c, "ok");

    VkApplicationInfo app = {0};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "graphvex";
    app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app.pEngineName = "graphvex";
    app.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    app.apiVersion = VK_API_VERSION_1_1;

    // MoltenVK needs the portability enumeration extension + flag.
    const char *exts[1];
    uint32_t extCount = 0;
    exts[extCount++] = "VK_KHR_portability_enumeration";

    VkInstanceCreateInfo ici = {0};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = extCount;
    ici.ppEnabledExtensionNames = exts;
    ici.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    if (vkCreateInstance(&ici, NULL, &c->instance) != VK_SUCCESS) {
        set_err(c, "vkCreateInstance failed (is a Vulkan/MoltenVK driver installed?)");
        Device_destroy(c);
        return NULL;
    }

    uint32_t count = 0;
    if (vkEnumeratePhysicalDevices(c->instance, &count, NULL) != VK_SUCCESS || count == 0) {
        set_err(c, "no Vulkan physical devices");
        Device_destroy(c);
        return NULL;
    }
    VkPhysicalDevice *devs = calloc(count, sizeof *devs);
    if (!devs) { Device_destroy(c); return NULL; }
    vkEnumeratePhysicalDevices(c->instance, &count, devs);
    c->physical = devs[0];
    free(devs);

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(c->physical, &props);
    snprintf(c->deviceName, sizeof c->deviceName, "%s", props.deviceName);

    uint32_t qn = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(c->physical, &qn, NULL);
    VkQueueFamilyProperties *qf = calloc(qn ? qn : 1, sizeof *qf);
    if (!qf) { Device_destroy(c); return NULL; }
    vkGetPhysicalDeviceQueueFamilyProperties(c->physical, &qn, qf);
    int chosen = -1;
    for (uint32_t i = 0; i < qn; i++) {
        if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { chosen = (int)i; break; }
    }
    free(qf);
    if (chosen < 0) {
        set_err(c, "no graphics queue family");
        Device_destroy(c);
        return NULL;
    }
    c->queueFamily = (uint32_t)chosen;

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {0};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = c->queueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;

    VkDeviceCreateInfo dci = {0};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    if (vkCreateDevice(c->physical, &dci, NULL, &c->device) != VK_SUCCESS) {
        set_err(c, "vkCreateDevice failed");
        Device_destroy(c);
        return NULL;
    }
    vkGetDeviceQueue(c->device, c->queueFamily, 0, &c->queue);
    return c;
}

void Device_destroy(Device *c) {
    if (!c) return;
    if (c->device) vkDestroyDevice(c->device, NULL);
    if (c->instance) vkDestroyInstance(c->instance, NULL);
    free(c);
}

bool Device_isValid(const Device *c) { return c && c->device != VK_NULL_HANDLE; }
const char *Device_lastError(const Device *c) { return c ? c->error : "null device"; }
const char *Device_name(const Device *c) { return c ? c->deviceName : ""; }
void *Device_native(const Device *c) { return c ? (void *)c->device : NULL; }
