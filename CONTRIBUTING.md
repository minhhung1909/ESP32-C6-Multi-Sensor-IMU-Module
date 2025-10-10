# Contributing to ESP32-C6 IMU Measurement Module

Thank you for your interest in contributing to this project! We welcome contributions from the community and appreciate your help in making this project better.

## 🤝 How to Contribute

### Reporting Issues
- Use the [GitHub Issues](https://github.com/YOUR_USERNAME/ESP32_Vibra_Accel_inclio_Module/issues) page
- Provide detailed information about the problem
- Include steps to reproduce the issue
- Attach relevant logs and screenshots

### Suggesting Features
- Use the [GitHub Discussions](https://github.com/YOUR_USERNAME/ESP32_Vibra_Accel_inclio_Module/discussions) page
- Describe the feature and its benefits
- Provide use cases and examples
- Consider implementation complexity

### Code Contributions
1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests if applicable
5. Submit a pull request

## 📋 Development Guidelines

### Code Style
- Follow ESP-IDF coding standards
- Use meaningful variable and function names
- Add comments for complex logic
- Keep functions small and focused

### Commit Messages
Use clear, descriptive commit messages:
```
feat: add WebSocket support for real-time data streaming
fix: resolve I2C timeout issue in magnetometer driver
docs: update API documentation for new endpoints
```

### Testing
- Test your changes on actual hardware
- Verify all sensors work correctly
- Check web interface functionality
- Ensure no memory leaks

## 🔧 Development Setup

### Prerequisites
- ESP-IDF v5.4 or later
- ESP32-C6 development board
- Required IMU sensors
- Git

### Setup Process
1. **Fork and clone**:
```bash
git clone https://github.com/YOUR_USERNAME/ESP32_Vibra_Accel_inclio_Module.git
cd ESP32_Vibra_Accel_inclio_Module
```

2. **Set up ESP-IDF**:
```bash
# Install ESP-IDF v5.4
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
git checkout v5.4
./install.sh esp32c6
. ./export.sh
```

3. **Test build**:
```bash
cd ESP32C6_IMU_WebMonitor
idf.py build
```

## 📁 Project Structure

```
ESP32_Vibra_Accel_inclio_Module/
├── ESP32C6_IIS2MDC/          # Magnetometer example
├── ESP32C6_IIS3DWBTR/        # High-speed accelerometer example
├── icm45686/                 # 6-axis IMU with APEX features
├── SCL3300/                  # Inclinometer example
├── ESP32C6_IMU_WebMonitor/   # Web-based monitoring system
│   ├── main/
│   │   ├── main.c            # Main application
│   │   ├── web_server.c      # Web server implementation
│   │   ├── imu_manager.c     # Sensor management
│   │   ├── data_buffer.c     # Data buffering
│   │   └── sensors/          # Sensor drivers
│   └── README.md
├── docs/                     # Documentation
├── .github/                  # GitHub workflows and templates
└── README.md
```

## 🎯 Areas for Contribution

### High Priority
- **Web Interface Improvements**: Better UI/UX, more charts
- **Performance Optimization**: Faster data processing
- **Documentation**: More examples and tutorials
- **Testing**: Unit tests and integration tests

### Medium Priority
- **New Sensor Support**: Additional IMU sensors
- **Cloud Integration**: AWS IoT, Azure IoT Hub
- **Mobile App**: Native mobile application
- **Advanced Analytics**: Statistical analysis features

### Low Priority
- **Multi-language Support**: Internationalization
- **Themes**: Customizable web interface themes
- **Plugins**: Plugin system for extensions
- **Advanced Configuration**: More configuration options

## 🐛 Bug Reports

When reporting bugs, please include:

### Required Information
- **ESP-IDF Version**: `idf.py --version`
- **Hardware**: ESP32-C6 board and sensor models
- **Steps to Reproduce**: Detailed steps
- **Expected Behavior**: What should happen
- **Actual Behavior**: What actually happens
- **Logs**: Relevant log output

### Optional Information
- **Screenshots**: If applicable
- **Configuration**: Custom configuration files
- **Environment**: Operating system, tools used

## 💡 Feature Requests

When suggesting features, please include:

### Required Information
- **Feature Description**: Clear description of the feature
- **Use Case**: Why this feature is needed
- **Benefits**: How it improves the project
- **Implementation Ideas**: If you have any

### Optional Information
- **Mockups**: UI/UX mockups if applicable
- **Examples**: Code examples or references
- **Priority**: How important this feature is

## 🔍 Code Review Process

### Pull Request Guidelines
1. **Clear Description**: Explain what the PR does
2. **Small Changes**: Keep PRs focused and small
3. **Tests**: Include tests for new features
4. **Documentation**: Update documentation if needed
5. **Backward Compatibility**: Ensure no breaking changes

### Review Criteria
- **Code Quality**: Clean, readable, well-commented code
- **Functionality**: Works as expected
- **Performance**: No performance regressions
- **Security**: No security vulnerabilities
- **Documentation**: Updated documentation

## 📚 Documentation Contributions

### Types of Documentation
- **Code Comments**: Inline documentation
- **API Documentation**: Function and API documentation
- **User Guides**: Step-by-step tutorials
- **Examples**: Code examples and demos
- **Troubleshooting**: Common issues and solutions

### Documentation Standards
- Use clear, concise language
- Include code examples
- Add diagrams when helpful
- Keep information up-to-date
- Use consistent formatting

## 🏆 Recognition

Contributors will be recognized in:
- **README.md**: Contributor list
- **Release Notes**: Feature contributions
- **Documentation**: Author credits
- **GitHub**: Contributor statistics

## 📞 Getting Help

### Communication Channels
- **GitHub Issues**: Bug reports and feature requests
- **GitHub Discussions**: General questions and discussions
- **Email**: your-email@example.com
- **Discord**: [Join our Discord server](https://discord.gg/YOUR_INVITE)

### Response Times
- **Issues**: Within 48 hours
- **Pull Requests**: Within 72 hours
- **Discussions**: Within 24 hours
- **Email**: Within 24 hours

## 💰 Support the Project

### Financial Support
- **PayPal Donate**: [Donate via PayPal](https://paypal.me/hbqtechnology)
- **GitHub Sponsors**: [Support on GitHub](https://github.com/sponsors/hbqtechnologycompany)
- **Buy Hardware**: [HBQ Technology Store](https://store.hbqsolution.com/)

### Non-Financial Support
- **Code Contributions**: Submit pull requests
- **Documentation**: Improve documentation
- **Testing**: Test on different hardware
- **Community**: Help other users

## 📄 License

By contributing to this project, you agree that your contributions will be licensed under the MIT License.

## 🙏 Thank You

Thank you for contributing to this project! Your contributions help make this project better for everyone in the community.

---

**Together, we can build amazing IoT solutions!**
